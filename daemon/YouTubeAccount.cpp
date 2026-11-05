#include "YouTubeAccount.h"
#include "Settings.h"

#include <KConfig>
#include <KConfigGroup>
#include <KWallet>

#include <QDBusConnection>
#include <QDebug>
#include <QDesktopServices>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QOAuth2AuthorizationCodeFlow>
#include <QOAuthHttpServerReplyHandler>
#include <QTimer>
#include <QUrlQuery>

namespace PlasmaTP {

static constexpr const char *AUTH_URL = "https://accounts.google.com/o/oauth2/v2/auth";
static constexpr const char *TOKEN_URL = "https://oauth2.googleapis.com/token";
static constexpr const char *REVOKE_URL = "https://oauth2.googleapis.com/revoke";
static constexpr const char *API_URL = "https://www.googleapis.com/youtube/v3/";
static constexpr const char *SCOPE = "https://www.googleapis.com/auth/youtube.readonly";

static constexpr const char *CONFIG_GROUP = "YouTube";
static constexpr const char *WALLET_FOLDER = "plasma-touchpad";
static constexpr const char *WALLET_KEY = "youtube-refresh-token";

// How long the subscription list is used before it is fetched again.
static constexpr qint64 SUBSCRIPTIONS_MAX_AGE_SECS = 6 * 60 * 60;
// YouTube API Services policy: data from the API may be stored for at most
// 30 days before it must be fetched again or deleted.
static constexpr qint64 CACHE_MAX_AGE_SECS = 30 * 24 * 60 * 60;

#ifndef PLASMA_TOUCHPAD_YOUTUBE_CLIENT_ID
#define PLASMA_TOUCHPAD_YOUTUBE_CLIENT_ID ""
#endif
#ifndef PLASMA_TOUCHPAD_YOUTUBE_CLIENT_SECRET
#define PLASMA_TOUCHPAD_YOUTUBE_CLIENT_SECRET ""
#endif

YouTubeAccount::YouTubeAccount(QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
{
    // The org.kde.plasma.touchpad service name is registered by WindowMover.
    if (!QDBusConnection::sessionBus().registerObject(
            QStringLiteral("/YouTube"), this,
            QDBusConnection::ExportScriptableSlots | QDBusConnection::ExportScriptableSignals))
        qWarning() << "YouTubeAccount: could not register DBus object /YouTube";

    KConfig cfg(QLatin1String(Settings::configFile()));
    const KConfigGroup grp = cfg.group(QLatin1String(CONFIG_GROUP));
    m_accountName = grp.readEntry("accountName", QString());
    m_subscriptions = QJsonDocument::fromJson(grp.readEntry("subscriptions", QByteArray("[]"))).array();
    m_subscriptionsFetched = grp.readEntry("subscriptionsFetched", QDateTime());
    expireCache();
    // The daemon runs for days at a time; keep checking.
    auto *expiry = new QTimer(this);
    connect(expiry, &QTimer::timeout, this, &YouTubeAccount::expireCache);
    expiry->start(60 * 60 * 1000);

    if (!loadClient())
        return;
    setState(State::SignedOut);
    // Signed in if KWallet holds a refresh token.
    openWallet([this](KWallet::Wallet *wallet) {
        QString token;
        if (wallet)
            wallet->readPassword(QLatin1String(WALLET_KEY), token);
        if (token.isEmpty()) {
            setState(State::SignedOut);
            return;
        }
        m_refreshToken = token;
        setState(State::SignedIn);
    });
}

YouTubeAccount::~YouTubeAccount() = default;

bool YouTubeAccount::loadClient()
{
    KConfig cfg(QLatin1String(Settings::configFile()));
    const KConfigGroup grp = cfg.group(QLatin1String(CONFIG_GROUP));
    m_clientId = grp.readEntry("clientId", QStringLiteral(PLASMA_TOUCHPAD_YOUTUBE_CLIENT_ID));
    m_clientSecret = grp.readEntry("clientSecret", QStringLiteral(PLASMA_TOUCHPAD_YOUTUBE_CLIENT_SECRET));
    if (m_clientId.isEmpty()) {
        setState(State::NotConfigured);
        return false;
    }
    return true;
}

QString YouTubeAccount::status()
{
    if (m_state == State::NotConfigured && loadClient())
        setState(State::SignedOut);
    switch (m_state) {
    case State::NotConfigured: return QStringLiteral("not-configured");
    case State::SignedOut:     return QStringLiteral("signed-out");
    case State::SigningIn:     return QStringLiteral("signing-in");
    case State::SignedIn:      return QStringLiteral("signed-in");
    }
    return {};
}

QString YouTubeAccount::accountName() const
{
    return m_state == State::SignedIn ? m_accountName : QString();
}

void YouTubeAccount::setState(State state)
{
    if (m_state == state)
        return;
    m_state = state;
    qDebug() << "YouTubeAccount: state" << int(state);
    Q_EMIT statusChanged();
}

void YouTubeAccount::openWallet(std::function<void(KWallet::Wallet *)> then)
{
    auto withFolder = [this, then]() {
        if (!m_wallet || !m_wallet->isOpen()) {
            then(nullptr);
            return;
        }
        if (!m_wallet->hasFolder(QLatin1String(WALLET_FOLDER)))
            m_wallet->createFolder(QLatin1String(WALLET_FOLDER));
        m_wallet->setFolder(QLatin1String(WALLET_FOLDER));
        then(m_wallet);
    };
    if (m_wallet && m_wallet->isOpen()) {
        withFolder();
        return;
    }
    // Asynchronous: unlocking the wallet may ask the user for a password.
    m_wallet = KWallet::Wallet::openWallet(KWallet::Wallet::LocalWallet(), 0,
                                           KWallet::Wallet::Asynchronous);
    if (!m_wallet) {
        qWarning() << "YouTubeAccount: KWallet is not available";
        then(nullptr);
        return;
    }
    connect(m_wallet, &KWallet::Wallet::walletOpened, this, [withFolder](bool ok) {
        if (!ok)
            qWarning() << "YouTubeAccount: could not open KWallet";
        withFolder();
    }, Qt::SingleShotConnection);
}

void YouTubeAccount::storeRefreshToken(const QString &token)
{
    openWallet([token](KWallet::Wallet *wallet) {
        if (!wallet) {
            qWarning() << "YouTubeAccount: sign-in will not survive a restart (no KWallet)";
            return;
        }
        if (token.isEmpty())
            wallet->removeEntry(QLatin1String(WALLET_KEY));
        else
            wallet->writePassword(QLatin1String(WALLET_KEY), token);
    });
}

void YouTubeAccount::signIn()
{
    if (!loadClient()) {
        qWarning() << "YouTubeAccount: no OAuth client configured";
        return;
    }
    if (m_state == State::SigningIn)
        return;

    delete m_flow;
    m_flow = new QOAuth2AuthorizationCodeFlow(m_network, this);
    m_flow->setAuthorizationUrl(QUrl(QLatin1String(AUTH_URL)));
    m_flow->setTokenUrl(QUrl(QLatin1String(TOKEN_URL)));
    m_flow->setClientIdentifier(m_clientId);
    m_flow->setClientIdentifierSharedKey(m_clientSecret);
    m_flow->setRequestedScopeTokens({QByteArray(SCOPE)});
    m_flow->setPkceMethod(QOAuth2AuthorizationCodeFlow::PkceMethod::S256);
    // Ask for a refresh token, and for it every time (Google only sends one
    // on first consent otherwise).
    m_flow->setModifyParametersFunction([](QAbstractOAuth::Stage stage, QMultiMap<QString, QVariant> *params) {
        if (stage == QAbstractOAuth::Stage::RequestingAuthorization) {
            params->insert(QStringLiteral("access_type"), QStringLiteral("offline"));
            params->insert(QStringLiteral("prompt"), QStringLiteral("consent"));
        }
    });

    // Loopback redirect, on a free port, only while signing in.
    delete m_replyHandler;
    m_replyHandler = new QOAuthHttpServerReplyHandler(QHostAddress::LocalHost, 0, this);
    if (!m_replyHandler->isListening()) {
        qWarning() << "YouTubeAccount: could not listen for the sign-in redirect";
        return;
    }
    m_replyHandler->setCallbackText(QStringLiteral(
        "<html><body style='font-family:sans-serif'><h2>Signed in</h2>"
        "<p>The dashboard Videos widget can now show your subscriptions. "
        "You can close this tab.</p></body></html>"));
    m_flow->setReplyHandler(m_replyHandler);

    connect(m_flow, &QAbstractOAuth::authorizeWithBrowser, this, [](const QUrl &url) {
        QDesktopServices::openUrl(url);
    });
    connect(m_flow, &QAbstractOAuth::granted, this, [this]() {
        m_replyHandler->close();
        m_accessToken = m_flow->token();
        m_accessTokenExpiry = m_flow->expirationAt();
        m_refreshToken = m_flow->refreshToken();
        storeRefreshToken(m_refreshToken);
        m_subscriptionsFetched = QDateTime();
        setState(State::SignedIn);
        fetchSubscriptions();
    });
    connect(m_flow, &QAbstractOAuth2::serverReportedErrorOccurred, this,
            [this](const QString &error, const QString &description, const QUrl &) {
        qWarning() << "YouTubeAccount: sign-in failed:" << error << description;
        m_replyHandler->close();
        setState(m_refreshToken.isEmpty() ? State::SignedOut : State::SignedIn);
    });
    connect(m_flow, &QAbstractOAuth::requestFailed, this, [this](QAbstractOAuth::Error error) {
        qWarning() << "YouTubeAccount: sign-in request failed:" << int(error);
        m_replyHandler->close();
        setState(m_refreshToken.isEmpty() ? State::SignedOut : State::SignedIn);
    });

    setState(State::SigningIn);
    m_flow->grant();
}

void YouTubeAccount::signOut()
{
    if (!m_refreshToken.isEmpty()) {
        // Tell Google to drop the grant; forget it here whatever the answer.
        QNetworkRequest req{QUrl(QLatin1String(REVOKE_URL))};
        req.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/x-www-form-urlencoded"));
        QUrlQuery body;
        body.addQueryItem(QStringLiteral("token"), m_refreshToken);
        QNetworkReply *reply = m_network->post(req, body.toString(QUrl::FullyEncoded).toUtf8());
        connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
    }
    forget();
}

void YouTubeAccount::forget()
{
    m_refreshToken.clear();
    m_accessToken.clear();
    m_accountName.clear();
    m_subscriptions = QJsonArray();
    m_subscriptionsFetched = QDateTime();
    storeRefreshToken(QString());
    saveCache();
    setState(m_clientId.isEmpty() ? State::NotConfigured : State::SignedOut);
    Q_EMIT subscriptionsChanged();
}

QString YouTubeAccount::subscriptions()
{
    if (m_state == State::SignedIn && !m_fetching
        && (!m_subscriptionsFetched.isValid()
            || m_subscriptionsFetched.secsTo(QDateTime::currentDateTimeUtc()) > SUBSCRIPTIONS_MAX_AGE_SECS))
        fetchSubscriptions();
    if (m_state != State::SignedIn)
        return QStringLiteral("[]");
    return QString::fromUtf8(QJsonDocument(m_subscriptions).toJson(QJsonDocument::Compact));
}

void YouTubeAccount::withAccessToken(std::function<void(const QString &)> then)
{
    if (!m_accessToken.isEmpty() && m_accessTokenExpiry.isValid()
        && QDateTime::currentDateTimeUtc().secsTo(m_accessTokenExpiry) > 60) {
        then(m_accessToken);
        return;
    }
    if (m_refreshToken.isEmpty()) {
        then(QString());
        return;
    }
    // Plain refresh-token grant; it needs no browser.
    QNetworkRequest req{QUrl(QLatin1String(TOKEN_URL))};
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  QStringLiteral("application/x-www-form-urlencoded"));
    QUrlQuery body;
    body.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("refresh_token"));
    body.addQueryItem(QStringLiteral("refresh_token"), m_refreshToken);
    body.addQueryItem(QStringLiteral("client_id"), m_clientId);
    if (!m_clientSecret.isEmpty())
        body.addQueryItem(QStringLiteral("client_secret"), m_clientSecret);
    QNetworkReply *reply = m_network->post(req, body.toString(QUrl::FullyEncoded).toUtf8());
    connect(reply, &QNetworkReply::finished, this, [this, reply, then]() {
        reply->deleteLater();
        const QJsonObject json = QJsonDocument::fromJson(reply->readAll()).object();
        const QString token = json.value(QStringLiteral("access_token")).toString();
        if (token.isEmpty()) {
            const QString error = json.value(QStringLiteral("error")).toString();
            qWarning() << "YouTubeAccount: token refresh failed:" << error << reply->errorString();
            // The grant was revoked (e.g. from the Google account page).
            if (error == QLatin1String("invalid_grant"))
                forget();
            then(QString());
            return;
        }
        m_accessToken = token;
        m_accessTokenExpiry = QDateTime::currentDateTimeUtc().addSecs(
            json.value(QStringLiteral("expires_in")).toInt(3600));
        then(token);
    });
}

void YouTubeAccount::fetchSubscriptions()
{
    m_fetching = true;
    withAccessToken([this](const QString &token) {
        if (token.isEmpty()) {
            m_fetching = false;
            return;
        }
        // Fetched with the list, so both are as fresh as
        // m_subscriptionsFetched says.
        fetchAccountName(token);
        fetchSubscriptionsPage(token, QString(), QJsonArray());
    });
}

void YouTubeAccount::fetchSubscriptionsPage(const QString &token, const QString &pageToken, QJsonArray collected)
{
    QUrl url(QLatin1String(API_URL) + QStringLiteral("subscriptions"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("part"), QStringLiteral("snippet"));
    query.addQueryItem(QStringLiteral("mine"), QStringLiteral("true"));
    query.addQueryItem(QStringLiteral("maxResults"), QStringLiteral("50"));
    query.addQueryItem(QStringLiteral("order"), QStringLiteral("alphabetical"));
    if (!pageToken.isEmpty())
        query.addQueryItem(QStringLiteral("pageToken"), pageToken);
    url.setQuery(query);

    QNetworkRequest req(url);
    req.setRawHeader("Authorization", "Bearer " + token.toUtf8());
    QNetworkReply *reply = m_network->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, token, collected]() mutable {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonObject json = QJsonDocument::fromJson(reply->readAll()).object();
        if (status != 200) {
            qWarning() << "YouTubeAccount: subscriptions request failed: HTTP" << status
                       << json.value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString();
            if (status == 401)
                m_accessToken.clear();
            m_fetching = false;
            return;
        }
        const QJsonArray items = json.value(QStringLiteral("items")).toArray();
        for (const QJsonValue &item : items) {
            const QJsonObject snippet = item.toObject().value(QStringLiteral("snippet")).toObject();
            const QString id = snippet.value(QStringLiteral("resourceId")).toObject()
                                   .value(QStringLiteral("channelId")).toString();
            if (!id.isEmpty())
                collected.append(QJsonObject{{QStringLiteral("id"), id},
                                             {QStringLiteral("title"), snippet.value(QStringLiteral("title"))}});
        }
        const QString next = json.value(QStringLiteral("nextPageToken")).toString();
        if (!next.isEmpty()) {
            fetchSubscriptionsPage(token, next, collected);
            return;
        }
        m_subscriptions = collected;
        m_subscriptionsFetched = QDateTime::currentDateTimeUtc();
        m_fetching = false;
        saveCache();
        qDebug() << "YouTubeAccount:" << m_subscriptions.size() << "subscriptions";
        Q_EMIT subscriptionsChanged();
    });
}

void YouTubeAccount::fetchAccountName(const QString &token)
{
    QUrl url(QLatin1String(API_URL) + QStringLiteral("channels"));
    url.setQuery(QStringLiteral("part=snippet&mine=true"));
    QNetworkRequest req(url);
    req.setRawHeader("Authorization", "Bearer " + token.toUtf8());
    QNetworkReply *reply = m_network->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        const QJsonArray items = QJsonDocument::fromJson(reply->readAll()).object()
                                     .value(QStringLiteral("items")).toArray();
        // An account without a YouTube channel has none; that's fine.
        const QString name = items.isEmpty() ? QString()
            : items.first().toObject().value(QStringLiteral("snippet")).toObject()
                  .value(QStringLiteral("title")).toString();
        if (name != m_accountName) {
            m_accountName = name;
            saveCache();
            Q_EMIT statusChanged();
        }
    });
}

void YouTubeAccount::expireCache()
{
    // A list without a timestamp (from an older version) counts as expired.
    const bool expired = m_subscriptionsFetched.isValid()
        ? m_subscriptionsFetched.secsTo(QDateTime::currentDateTimeUtc()) > CACHE_MAX_AGE_SECS
        : (!m_subscriptions.isEmpty() || !m_accountName.isEmpty());
    if (!expired)
        return;
    qDebug() << "YouTubeAccount: cached YouTube data is over 30 days old; deleting it";
    m_subscriptions = QJsonArray();
    m_accountName.clear();
    m_subscriptionsFetched = QDateTime();
    saveCache();
    Q_EMIT subscriptionsChanged();
    Q_EMIT statusChanged();
}

void YouTubeAccount::saveCache()
{
    KConfig cfg(QLatin1String(Settings::configFile()));
    KConfigGroup grp = cfg.group(QLatin1String(CONFIG_GROUP));
    grp.writeEntry("accountName", m_accountName);
    grp.writeEntry("subscriptions", QJsonDocument(m_subscriptions).toJson(QJsonDocument::Compact));
    if (m_subscriptionsFetched.isValid())
        grp.writeEntry("subscriptionsFetched", m_subscriptionsFetched);
    else
        grp.deleteEntry("subscriptionsFetched");
    cfg.sync();
}

} // namespace PlasmaTP
