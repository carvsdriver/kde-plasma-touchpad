#pragma once

#include <QDateTime>
#include <QJsonArray>
#include <QObject>
#include <QPointer>
#include <QString>

#include <functional>

class QNetworkAccessManager;
class QNetworkReply;
class QOAuth2AuthorizationCodeFlow;
class QOAuthHttpServerReplyHandler;

namespace KWallet {
class Wallet;
}

namespace PlasmaTP {

/*
 * YouTubeAccount signs the user in to YouTube, read-only, so the dashboard
 * Videos widget can follow the channels they subscribe to.
 *
 * Sign-in is OAuth 2.0 for installed apps: Google's consent page opens in the
 * default browser (so the browser's password manager, passkeys and
 * extensions all work), and Google redirects back to a loopback address that
 * listens only during sign-in.  PKCE protects the exchange.  The refresh
 * token is kept in KWallet; nothing secret goes in a config file.
 *
 * Only the subscription list comes from the YouTube Data API (scope
 * youtube.readonly).  It is cached in plasma-touchpadrc [YouTube] and
 * refreshed every few hours; the widget still reads each channel's public
 * feed for the videos themselves.
 *
 * The OAuth client (a Google Cloud "Desktop app" client) comes from
 * plasma-touchpadrc [YouTube] clientId / clientSecret, or from the build
 * (PLASMA_TOUCHPAD_YOUTUBE_CLIENT_ID / _SECRET).  For an installed app the
 * "secret" is not confidential; Google issues one anyway.
 *
 * D-Bus: org.kde.plasma.touchpad /YouTube, interface
 * org.kde.plasma.touchpad.YouTube.
 */
class YouTubeAccount : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.plasma.touchpad.YouTube")

public:
    explicit YouTubeAccount(QObject *parent = nullptr);
    ~YouTubeAccount() override;

public Q_SLOTS:
    // "not-configured" (no OAuth client), "signed-out", "signing-in" or
    // "signed-in".  Picks up an OAuth client added to the config file.
    Q_SCRIPTABLE QString status();
    // The signed-in account's channel name, if known.
    Q_SCRIPTABLE QString accountName() const;
    // The signed-in account's channel avatar (an image URL), if known.
    Q_SCRIPTABLE QString accountIcon() const;
    // Open Google's sign-in page in the default browser.
    Q_SCRIPTABLE void signIn();
    // Revoke access and forget the account.
    Q_SCRIPTABLE void signOut();
    // The subscribed channels as a JSON array of {"id", "title", "icon"}; the cached
    // list, refreshed in the background when it is old.
    Q_SCRIPTABLE QString subscriptions();

Q_SIGNALS:
    Q_SCRIPTABLE void statusChanged();
    Q_SCRIPTABLE void subscriptionsChanged();

private:
    enum class State { NotConfigured, SignedOut, SigningIn, SignedIn };

    void setState(State state);
    bool loadClient();
    void openWallet(std::function<void(KWallet::Wallet *)> then);
    void storeRefreshToken(const QString &token);
    // Call `then` with a valid access token (refreshing it if needed), or
    // with an empty string if that fails.
    void withAccessToken(std::function<void(const QString &)> then);
    void fetchSubscriptions();
    void fetchSubscriptionsPage(const QString &token, const QString &pageToken, QJsonArray collected);
    void fetchAccountName(const QString &token);
    // Access was revoked or the refresh token is no longer valid.
    void forget();
    void saveCache();
    // Delete cached API data older than 30 days (YouTube API policy).
    void expireCache();

    State m_state = State::NotConfigured;
    QString m_clientId;
    QString m_clientSecret;
    QString m_refreshToken;
    QString m_accessToken;
    QDateTime m_accessTokenExpiry;
    QString m_accountName;
    QString m_accountIcon;
    QJsonArray m_subscriptions;
    QDateTime m_subscriptionsFetched;
    bool m_fetching = false;

    QNetworkAccessManager *m_network;
    QOAuth2AuthorizationCodeFlow *m_flow = nullptr;
    QOAuthHttpServerReplyHandler *m_replyHandler = nullptr;
    QPointer<KWallet::Wallet> m_wallet;
};

} // namespace PlasmaTP
