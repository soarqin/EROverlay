#pragma once

#if defined(__cplusplus)
extern "C" {
#else
#include <stdbool.h>
#endif

#if defined( STEAM_API_EXPORTS )
#define S_API extern "C" __declspec( dllexport )
#elif defined( STEAM_API_NODLL )
#define S_API extern "C"
#else
#define S_API extern "C" __declspec( dllimport )
#endif // STEAM_API_EXPORTS

#define S_CALLTYPE __cdecl

typedef unsigned char uint8;
typedef signed char int8;

#if defined( _WIN32 ) && !defined( __GNUC__ )

typedef __int16 int16;
typedef unsigned __int16 uint16;
typedef __int32 int32;
typedef unsigned __int32 uint32;
typedef __int64 int64;
typedef unsigned __int64 uint64;

typedef int64 lint64;
typedef uint64 ulint64;

#ifdef X64BITS
typedef __int64 intp;				// intp is an integer that can accomodate a pointer
typedef unsigned __int64 uintp;		// (ie, sizeof(intp) >= sizeof(int) && sizeof(intp) >= sizeof(void *)
#else
typedef __int32 intp;
typedef unsigned __int32 uintp;
#endif

#else // _WIN32

typedef short int16;
typedef unsigned short uint16;
typedef int int32;
typedef unsigned int uint32;
typedef long long int64;
typedef unsigned long long uint64;

// [u]int64 are actually defined as 'long long' and gcc 64-bit
// doesn't automatically consider them the same as 'long int'.
// Changing the types for [u]int64 is complicated by
// there being many definitions, so we just
// define a 'long int' here and use it in places that would
// otherwise confuse the compiler.
typedef long int lint64;
typedef unsigned long int ulint64;

#ifdef X64BITS
typedef long long intp;
typedef unsigned long long uintp;
#else
typedef int intp;
typedef unsigned int uintp;
#endif

#endif // else _WIN32

typedef uint32 AppId_t;
const AppId_t k_uAppIdInvalid = 0x0;

// AppIds and DepotIDs also presently share the same namespace
typedef uint32 DepotId_t;
const DepotId_t k_uDepotIdInvalid = 0x0;

// RTime32.  Seconds elapsed since Jan 1 1970, i.e. unix timestamp.
// It's the same as time_t, but it is always 32-bit and unsigned.
typedef uint32 RTime32;

// handle to a Steam API call
typedef uint64 SteamAPICall_t;
const SteamAPICall_t k_uAPICallInvalid = 0x0;

typedef uint32 AccountID_t;
const AccountID_t k_uAccountIdInvalid = 0;

// Party Beacon ID
typedef uint64 PartyBeaconID_t;
const PartyBeaconID_t k_ulPartyBeaconIdInvalid = 0;

// handle to a communication pipe to the Steam client
typedef int32 HSteamPipe;
// handle to single instance of a steam user
typedef int32 HSteamUser;

typedef uint64 uint64_steamid; // Used when passing or returning CSteamID
typedef uint64 uint64_gameid; // Used when passing or return CGameID

typedef void ( S_CALLTYPE *SteamAPIWarningMessageHook_t )( int, const char * );
typedef uint32 ( S_CALLTYPE *SteamAPI_CheckCallbackRegistered_t )( int iCallbackNum );

#define STEAM_TYPE_NOBODY(n) typedef struct n n
STEAM_TYPE_NOBODY(ISteamUser);
STEAM_TYPE_NOBODY(ISteamGameServer);
STEAM_TYPE_NOBODY(ISteamUtils);
STEAM_TYPE_NOBODY(ISteamFriends);
STEAM_TYPE_NOBODY(ISteamMatchmaking);
STEAM_TYPE_NOBODY(ISteamMatchmakingServers);
STEAM_TYPE_NOBODY(ISteamUserStats);
STEAM_TYPE_NOBODY(ISteamGameServerStats);
STEAM_TYPE_NOBODY(ISteamNetworking);
STEAM_TYPE_NOBODY(ISteamRemoteStorage);
STEAM_TYPE_NOBODY(ISteamApps);
STEAM_TYPE_NOBODY(ISteamClient);
STEAM_TYPE_NOBODY(ISteamRemotePlay);
STEAM_TYPE_NOBODY(ISteamParties);
STEAM_TYPE_NOBODY(ISteamInput);
STEAM_TYPE_NOBODY(ISteamParentalSettings);
STEAM_TYPE_NOBODY(ISteamVideo);
STEAM_TYPE_NOBODY(ISteamInventory);
STEAM_TYPE_NOBODY(ISteamHTMLSurface);
STEAM_TYPE_NOBODY(ISteamMusicRemote);
STEAM_TYPE_NOBODY(ISteamMusic);
STEAM_TYPE_NOBODY(ISteamUGC);
STEAM_TYPE_NOBODY(ISteamController);
STEAM_TYPE_NOBODY(ISteamHTTP);
STEAM_TYPE_NOBODY(ISteamGameSearch);
STEAM_TYPE_NOBODY(ISteamScreenshots);
STEAM_TYPE_NOBODY(SteamNetworkingIdentity);
STEAM_TYPE_NOBODY(ISteamMatchmakingServerListResponse);
STEAM_TYPE_NOBODY(ISteamMatchmakingPingResponse);
STEAM_TYPE_NOBODY(ISteamMatchmakingPlayersResponse);
STEAM_TYPE_NOBODY(ISteamMatchmakingRulesResponse);
STEAM_TYPE_NOBODY(gameserveritem_t);
STEAM_TYPE_NOBODY(MatchMakingKeyValuePair_t);
STEAM_TYPE_NOBODY(ISteamNetworkingSockets);
STEAM_TYPE_NOBODY(ISteamNetworkingMessages);
STEAM_TYPE_NOBODY(SteamNetworkingMessage_t);
STEAM_TYPE_NOBODY(SteamNetConnectionRealTimeStatus_t);
STEAM_TYPE_NOBODY(SteamNetworkingIPAddr);
STEAM_TYPE_NOBODY(SteamNetworkingConfigValue_t);
STEAM_TYPE_NOBODY(SteamNetConnectionRealTimeLaneStatus_t);
STEAM_TYPE_NOBODY(SteamNetAuthenticationStatus_t);
STEAM_TYPE_NOBODY(SteamDatagramRelayAuthTicket);
STEAM_TYPE_NOBODY(SteamDatagramHostedAddress);
STEAM_TYPE_NOBODY(SteamDatagramGameCoordinatorServerLogin);
STEAM_TYPE_NOBODY(SteamNetConnectionStatusChangedCallback_t);
STEAM_TYPE_NOBODY(SteamRelayNetworkStatus_t);
STEAM_TYPE_NOBODY(SteamNetworkingMessagesSessionRequest_t);
STEAM_TYPE_NOBODY(SteamNetworkingMessagesSessionFailed_t);
STEAM_TYPE_NOBODY(SteamNetworkingFakeIPResult_t);
STEAM_TYPE_NOBODY(ISteamNetworkingUtils);
STEAM_TYPE_NOBODY(SteamNetConnectionInfo_t);
STEAM_TYPE_NOBODY(ISteamTimeline);
STEAM_TYPE_NOBODY(ISteamNetworkingConnectionSignaling);
STEAM_TYPE_NOBODY(ISteamNetworkingSignalingRecvContext);
STEAM_TYPE_NOBODY(ISteamNetworkingFakeUDPPort);
STEAM_TYPE_NOBODY(SteamNetworkPingLocation_t);
STEAM_TYPE_NOBODY(servernetadr_t);

#pragma pack( push, 8 )

// General result codes
typedef enum EResult
{
    k_EResultNone = 0,							// no result
    k_EResultOK	= 1,							// success
    k_EResultFail = 2,							// generic failure
    k_EResultNoConnection = 3,					// no/failed network connection
//	k_EResultNoConnectionRetry = 4,				// OBSOLETE - removed
    k_EResultInvalidPassword = 5,				// password/ticket is invalid
    k_EResultLoggedInElsewhere = 6,				// same user logged in elsewhere
    k_EResultInvalidProtocolVer = 7,			// protocol version is incorrect
    k_EResultInvalidParam = 8,					// a parameter is incorrect
    k_EResultFileNotFound = 9,					// file was not found
    k_EResultBusy = 10,							// called method busy - action not taken
    k_EResultInvalidState = 11,					// called object was in an invalid state
    k_EResultInvalidName = 12,					// name is invalid
    k_EResultInvalidEmail = 13,					// email is invalid
    k_EResultDuplicateName = 14,				// name is not unique
    k_EResultAccessDenied = 15,					// access is denied
    k_EResultTimeout = 16,						// operation timed out
    k_EResultBanned = 17,						// VAC2 banned
    k_EResultAccountNotFound = 18,				// account not found
    k_EResultInvalidSteamID = 19,				// steamID is invalid
    k_EResultServiceUnavailable = 20,			// The requested service is currently unavailable
    k_EResultNotLoggedOn = 21,					// The user is not logged on
    k_EResultPending = 22,						// Request is pending (may be in process, or waiting on third party)
    k_EResultEncryptionFailure = 23,			// Encryption or Decryption failed
    k_EResultInsufficientPrivilege = 24,		// Insufficient privilege
    k_EResultLimitExceeded = 25,				// Too much of a good thing
    k_EResultRevoked = 26,						// Access has been revoked (used for revoked guest passes)
    k_EResultExpired = 27,						// License/Guest pass the user is trying to access is expired
    k_EResultAlreadyRedeemed = 28,				// Guest pass has already been redeemed by account, cannot be acked again
    k_EResultDuplicateRequest = 29,				// The request is a duplicate and the action has already occurred in the past, ignored this time
    k_EResultAlreadyOwned = 30,					// All the games in this guest pass redemption request are already owned by the user
    k_EResultIPNotFound = 31,					// IP address not found
    k_EResultPersistFailed = 32,				// failed to write change to the data store
    k_EResultLockingFailed = 33,				// failed to acquire access lock for this operation
    k_EResultLogonSessionReplaced = 34,
    k_EResultConnectFailed = 35,
    k_EResultHandshakeFailed = 36,
    k_EResultIOFailure = 37,
    k_EResultRemoteDisconnect = 38,
    k_EResultShoppingCartNotFound = 39,			// failed to find the shopping cart requested
    k_EResultBlocked = 40,						// a user didn't allow it
    k_EResultIgnored = 41,						// target is ignoring sender
    k_EResultNoMatch = 42,						// nothing matching the request found
    k_EResultAccountDisabled = 43,
    k_EResultServiceReadOnly = 44,				// this service is not accepting content changes right now
    k_EResultAccountNotFeatured = 45,			// account doesn't have value, so this feature isn't available
    k_EResultAdministratorOK = 46,				// allowed to take this action, but only because requester is admin
    k_EResultContentVersion = 47,				// A Version mismatch in content transmitted within the Steam protocol.
    k_EResultTryAnotherCM = 48,					// The current CM can't service the user making a request, user should try another.
    k_EResultPasswordRequiredToKickSession = 49,// You are already logged in elsewhere, this cached credential login has failed.
    k_EResultAlreadyLoggedInElsewhere = 50,		// You are already logged in elsewhere, you must wait
    k_EResultSuspended = 51,					// Long running operation (content download) suspended/paused
    k_EResultCancelled = 52,					// Operation canceled (typically by user: content download)
    k_EResultDataCorruption = 53,				// Operation canceled because data is ill formed or unrecoverable
    k_EResultDiskFull = 54,						// Operation canceled - not enough disk space.
    k_EResultRemoteCallFailed = 55,				// an remote call or IPC call failed
    k_EResultPasswordUnset = 56,				// Password could not be verified as it's unset server side
    k_EResultExternalAccountUnlinked = 57,		// External account (PSN, Facebook...) is not linked to a Steam account
    k_EResultPSNTicketInvalid = 58,				// PSN ticket was invalid
    k_EResultExternalAccountAlreadyLinked = 59,	// External account (PSN, Facebook...) is already linked to some other account, must explicitly request to replace/delete the link first
    k_EResultRemoteFileConflict = 60,			// The sync cannot resume due to a conflict between the local and remote files
    k_EResultIllegalPassword = 61,				// The requested new password is not legal
    k_EResultSameAsPreviousValue = 62,			// new value is the same as the old one ( secret question and answer )
    k_EResultAccountLogonDenied = 63,			// account login denied due to 2nd factor authentication failure
    k_EResultCannotUseOldPassword = 64,			// The requested new password is not legal
    k_EResultInvalidLoginAuthCode = 65,			// account login denied due to auth code invalid
    k_EResultAccountLogonDeniedNoMail = 66,		// account login denied due to 2nd factor auth failure - and no mail has been sent - partner site specific
    k_EResultHardwareNotCapableOfIPT = 67,		//
    k_EResultIPTInitError = 68,					//
    k_EResultParentalControlRestricted = 69,	// operation failed due to parental control restrictions for current user
    k_EResultFacebookQueryError = 70,			// Facebook query returned an error
    k_EResultExpiredLoginAuthCode = 71,			// account login denied due to auth code expired
    k_EResultIPLoginRestrictionFailed = 72,
    k_EResultAccountLockedDown = 73,
    k_EResultAccountLogonDeniedVerifiedEmailRequired = 74,
    k_EResultNoMatchingURL = 75,
    k_EResultBadResponse = 76,					// parse failure, missing field, etc.
    k_EResultRequirePasswordReEntry = 77,		// The user cannot complete the action until they re-enter their password
    k_EResultValueOutOfRange = 78,				// the value entered is outside the acceptable range
    k_EResultUnexpectedError = 79,				// something happened that we didn't expect to ever happen
    k_EResultDisabled = 80,						// The requested service has been configured to be unavailable
    k_EResultInvalidCEGSubmission = 81,			// The set of files submitted to the CEG server are not valid !
    k_EResultRestrictedDevice = 82,				// The device being used is not allowed to perform this action
    k_EResultRegionLocked = 83,					// The action could not be complete because it is region restricted
    k_EResultRateLimitExceeded = 84,			// Temporary rate limit exceeded, try again later, different from k_EResultLimitExceeded which may be permanent
    k_EResultAccountLoginDeniedNeedTwoFactor = 85,	// Need two-factor code to login
    k_EResultItemDeleted = 86,					// The thing we're trying to access has been deleted
    k_EResultAccountLoginDeniedThrottle = 87,	// login attempt failed, try to throttle response to possible attacker
    k_EResultTwoFactorCodeMismatch = 88,		// two factor code mismatch
    k_EResultTwoFactorActivationCodeMismatch = 89,	// activation code for two-factor didn't match
    k_EResultAccountAssociatedToMultiplePartners = 90,	// account has been associated with multiple partners
    k_EResultNotModified = 91,					// data not modified
    k_EResultNoMobileDevice = 92,				// the account does not have a mobile device associated with it
    k_EResultTimeNotSynced = 93,				// the time presented is out of range or tolerance
    k_EResultSmsCodeFailed = 94,				// SMS code failure (no match, none pending, etc.)
    k_EResultAccountLimitExceeded = 95,			// Too many accounts access this resource
    k_EResultAccountActivityLimitExceeded = 96,	// Too many changes to this account
    k_EResultPhoneActivityLimitExceeded = 97,	// Too many changes to this phone
    k_EResultRefundToWallet = 98,				// Cannot refund to payment method, must use wallet
    k_EResultEmailSendFailure = 99,				// Cannot send an email
    k_EResultNotSettled = 100,					// Can't perform operation till payment has settled
    k_EResultNeedCaptcha = 101,					// Needs to provide a valid captcha
    k_EResultGSLTDenied = 102,					// a game server login token owned by this token's owner has been banned
    k_EResultGSOwnerDenied = 103,				// game server owner is denied for other reason (account lock, community ban, vac ban, missing phone)
    k_EResultInvalidItemType = 104,				// the type of thing we were requested to act on is invalid
    k_EResultIPBanned = 105,					// the ip address has been banned from taking this action
    k_EResultGSLTExpired = 106,					// this token has expired from disuse; can be reset for use
    k_EResultInsufficientFunds = 107,			// user doesn't have enough wallet funds to complete the action
    k_EResultTooManyPending = 108,				// There are too many of this thing pending already
    k_EResultNoSiteLicensesFound = 109,			// No site licenses found
    k_EResultWGNetworkSendExceeded = 110,		// the WG couldn't send a response because we exceeded max network send size
    k_EResultAccountNotFriends = 111,			// the user is not mutually friends
    k_EResultLimitedUserAccount = 112,			// the user is limited
    k_EResultCantRemoveItem = 113,				// item can't be removed
    k_EResultAccountDeleted = 114,				// account has been deleted
    k_EResultExistingUserCancelledLicense = 115,	// A license for this already exists, but cancelled
    k_EResultCommunityCooldown = 116,			// access is denied because of a community cooldown (probably from support profile data resets)
    k_EResultNoLauncherSpecified = 117,			// No launcher was specified, but a launcher was needed to choose correct realm for operation.
    k_EResultMustAgreeToSSA = 118,				// User must agree to china SSA or global SSA before login
    k_EResultLauncherMigrated = 119,			// The specified launcher type is no longer supported; the user should be directed elsewhere
    k_EResultSteamRealmMismatch = 120,			// The user's realm does not match the realm of the requested resource
    k_EResultInvalidSignature = 121,			// signature check did not match
    k_EResultParseFailure = 122,				// Failed to parse input
    k_EResultNoVerifiedPhone = 123,				// account does not have a verified phone number
    k_EResultInsufficientBattery = 124,			// user device doesn't have enough battery charge currently to complete the action
    k_EResultChargerRequired = 125,				// The operation requires a charger to be plugged in, which wasn't present
    k_EResultCachedCredentialInvalid = 126,		// Cached credential was invalid - user must reauthenticate
    K_EResultPhoneNumberIsVOIP = 127,			// The phone number provided is a Voice Over IP number
    k_EResultNotSupported = 128,				// The data being accessed is not supported by this API
    k_EResultFamilySizeLimitExceeded = 129,		// Reached the maximum size of the family
} EResult;

// Error codes for use with the voice functions
typedef enum EVoiceResult
{
    k_EVoiceResultOK = 0,
    k_EVoiceResultNotInitialized = 1,
    k_EVoiceResultNotRecording = 2,
    k_EVoiceResultNoData = 3,
    k_EVoiceResultBufferTooSmall = 4,
    k_EVoiceResultDataCorrupted = 5,
    k_EVoiceResultRestricted = 6,
    k_EVoiceResultUnsupportedCodec = 7,
    k_EVoiceResultReceiverOutOfDate = 8,
    k_EVoiceResultReceiverDidNotAnswer = 9,

} EVoiceResult;

// Result codes to GSHandleClientDeny/Kick
typedef enum EDenyReason
{
    k_EDenyInvalid = 0,
    k_EDenyInvalidVersion = 1,
    k_EDenyGeneric = 2,
    k_EDenyNotLoggedOn = 3,
    k_EDenyNoLicense = 4,
    k_EDenyCheater = 5,
    k_EDenyLoggedInElseWhere = 6,
    k_EDenyUnknownText = 7,
    k_EDenyIncompatibleAnticheat = 8,
    k_EDenyMemoryCorruption = 9,
    k_EDenyIncompatibleSoftware = 10,
    k_EDenySteamConnectionLost = 11,
    k_EDenySteamConnectionError = 12,
    k_EDenySteamResponseTimedOut = 13,
    k_EDenySteamValidationStalled = 14,
    k_EDenySteamOwnerLeftGuestUser = 15,
} EDenyReason;

// return type of GetAuthSessionTicket
typedef uint32 HAuthTicket;
const HAuthTicket k_HAuthTicketInvalid = 0;

// results from BeginAuthSession
typedef enum EBeginAuthSessionResult
{
    k_EBeginAuthSessionResultOK = 0,						// Ticket is valid for this game and this steamID.
    k_EBeginAuthSessionResultInvalidTicket = 1,				// Ticket is not valid.
    k_EBeginAuthSessionResultDuplicateRequest = 2,			// A ticket has already been submitted for this steamID
    k_EBeginAuthSessionResultInvalidVersion = 3,			// Ticket is from an incompatible interface version
    k_EBeginAuthSessionResultGameMismatch = 4,				// Ticket is not for this game
    k_EBeginAuthSessionResultExpiredTicket = 5,				// Ticket has expired
} EBeginAuthSessionResult;

// Callback values for callback ValidateAuthTicketResponse_t which is a response to BeginAuthSession
typedef enum EAuthSessionResponse
{
    k_EAuthSessionResponseOK = 0,							// Steam has verified the user is online, the ticket is valid and ticket has not been reused.
    k_EAuthSessionResponseUserNotConnectedToSteam = 1,		// The user in question is not connected to steam
    k_EAuthSessionResponseNoLicenseOrExpired = 2,			// The license has expired.
    k_EAuthSessionResponseVACBanned = 3,					// The user is VAC banned for this game.
    k_EAuthSessionResponseLoggedInElseWhere = 4,			// The user account has logged in elsewhere and the session containing the game instance has been disconnected.
    k_EAuthSessionResponseVACCheckTimedOut = 5,				// VAC has been unable to perform anti-cheat checks on this user
    k_EAuthSessionResponseAuthTicketCanceled = 6,			// The ticket has been canceled by the issuer
    k_EAuthSessionResponseAuthTicketInvalidAlreadyUsed = 7,	// This ticket has already been used, it is not valid.
    k_EAuthSessionResponseAuthTicketInvalid = 8,			// This ticket is not from a user instance currently connected to steam.
    k_EAuthSessionResponsePublisherIssuedBan = 9,			// The user is banned for this game. The ban came via the web api and not VAC
    k_EAuthSessionResponseAuthTicketNetworkIdentityFailure = 10,	// The network identity in the ticket does not match the server authenticating the ticket
} EAuthSessionResponse;

// results from UserHasLicenseForApp
typedef enum EUserHasLicenseForAppResult
{
    k_EUserHasLicenseResultHasLicense = 0,					// User has a license for specified app
    k_EUserHasLicenseResultDoesNotHaveLicense = 1,			// User does not have a license for the specified app
    k_EUserHasLicenseResultNoAuth = 2,						// User has not been authenticated
} EUserHasLicenseForAppResult;


// Steam account types
typedef enum EAccountType
{
    k_EAccountTypeInvalid = 0,
    k_EAccountTypeIndividual = 1,		// single user account
    k_EAccountTypeMultiseat = 2,		// multiseat (e.g. cybercafe) account
    k_EAccountTypeGameServer = 3,		// game server account
    k_EAccountTypeAnonGameServer = 4,	// anonymous game server account
    k_EAccountTypePending = 5,			// pending
    k_EAccountTypeContentServer = 6,	// content server
    k_EAccountTypeClan = 7,
    k_EAccountTypeChat = 8,
    k_EAccountTypeConsoleUser = 9,		// Fake SteamID for local PSN account on PS3 or Live account on 360, etc.
    k_EAccountTypeAnonUser = 10,

    // Max of 16 items in this field
    k_EAccountTypeMax
} EAccountType;

//
// Specifies a game's online state in relation to duration control
//
typedef enum EDurationControlOnlineState
{
    k_EDurationControlOnlineState_Invalid = 0,				// nil value
    k_EDurationControlOnlineState_Offline = 1,				// currently in offline play - single-player, offline co-op, etc.
    k_EDurationControlOnlineState_Online = 2,				// currently in online play
    k_EDurationControlOnlineState_OnlineHighPri = 3,		// currently in online play and requests not to be interrupted
} EDurationControlOnlineState;

//-----------------------------------------------------------------------------
// Purpose: set of relationships to other users
//-----------------------------------------------------------------------------
typedef enum EFriendRelationship
{
    k_EFriendRelationshipNone = 0,
    k_EFriendRelationshipBlocked = 1,			// this doesn't get stored; the user has just done an Ignore on an friendship invite
    k_EFriendRelationshipRequestRecipient = 2,
    k_EFriendRelationshipFriend = 3,
    k_EFriendRelationshipRequestInitiator = 4,
    k_EFriendRelationshipIgnored = 5,			// this is stored; the user has explicit blocked this other user from comments/chat/etc
    k_EFriendRelationshipIgnoredFriend = 6,
    k_EFriendRelationshipSuggested_DEPRECATED = 7,		// was used by the original implementation of the facebook linking feature, but now unused.

    // keep this updated
    k_EFriendRelationshipMax = 8,
} EFriendRelationship;

// maximum length of friend group name (not including terminating nul!)
const int k_cchMaxFriendsGroupName = 64;

// maximum number of groups a single user is allowed
const int k_cFriendsGroupLimit = 100;

// friends group identifier type
typedef int16 FriendsGroupID_t;

// invalid friends group identifier constant
const FriendsGroupID_t k_FriendsGroupID_Invalid = -1;

const int k_cEnumerateFollowersMax = 50;

//-----------------------------------------------------------------------------
// Purpose: list of states a friend can be in
//-----------------------------------------------------------------------------
typedef enum EPersonaState
{
    k_EPersonaStateOffline = 0,			// friend is not currently logged on
    k_EPersonaStateOnline = 1,			// friend is logged on
    k_EPersonaStateBusy = 2,			// user is on, but busy
    k_EPersonaStateAway = 3,			// auto-away feature
    k_EPersonaStateSnooze = 4,			// auto-away for a long time
    k_EPersonaStateLookingToTrade = 5,	// Online, trading
    k_EPersonaStateLookingToPlay = 6,	// Online, wanting to play
    k_EPersonaStateInvisible = 7,		// Online, but appears offline to friends.  This status is never published to clients.
    k_EPersonaStateMax,
} EPersonaState;

// Steam universes.  Each universe is a self-contained Steam instance.
typedef enum EUniverse
{
    k_EUniverseInvalid = 0,
    k_EUniversePublic = 1,
    k_EUniverseBeta = 2,
    k_EUniverseInternal = 3,
    k_EUniverseDev = 4,
    // k_EUniverseRC = 5,				// no such universe anymore
    k_EUniverseMax
} EUniverse;

typedef enum ESteamIPType
{
    k_ESteamIPTypeIPv4 = 0,
    k_ESteamIPTypeIPv6 = 1,
} ESteamIPType;

typedef struct SteamIPAddress_t
{
    union {

        uint32			m_unIPv4;		// Host order
        uint8			m_rgubIPv6[16];		// Network order! Same as inaddr_in6.  (0011:2233:4455:6677:8899:aabb:ccdd:eeff)

        // Internal use only
        uint64			m_ipv6Qword[2];	// big endian
    };

    ESteamIPType m_eType;

#if defined( __cplusplus )
    bool IsSet() const
    {
        if ( k_ESteamIPTypeIPv4 == m_eType )
        {
            return m_unIPv4 != 0;
        }
        else
        {
            return m_ipv6Qword[0] !=0 || m_ipv6Qword[1] != 0;
        }
    }

    static SteamIPAddress_t IPv4Any()
    {
        SteamIPAddress_t ipOut;
        ipOut.m_eType = k_ESteamIPTypeIPv4;
        ipOut.m_unIPv4 = 0;

        return ipOut;
    }

    static SteamIPAddress_t IPv6Any()
    {
        SteamIPAddress_t ipOut;
        ipOut.m_eType = k_ESteamIPTypeIPv6;
        ipOut.m_ipv6Qword[0] = 0;
        ipOut.m_ipv6Qword[1] = 0;

        return ipOut;
    }

    static SteamIPAddress_t IPv4Loopback()
    {
        SteamIPAddress_t ipOut;
        ipOut.m_eType = k_ESteamIPTypeIPv4;
        ipOut.m_unIPv4 = 0x7f000001;

        return ipOut;
    }

    static SteamIPAddress_t IPv6Loopback()
    {
        SteamIPAddress_t ipOut;
        ipOut.m_eType = k_ESteamIPTypeIPv6;
        ipOut.m_ipv6Qword[0] = 0;
        ipOut.m_ipv6Qword[1] = 0;
        ipOut.m_rgubIPv6[15] = 1;

        return ipOut;
    }
#endif
} SteamIPAddress_t;

typedef struct CSteamID {
    union SteamID_t
    {
        struct SteamIDComponent_t
        {
            uint32				m_unAccountID : 32;			// unique account identifier
            unsigned int		m_unAccountInstance : 20;	// dynamic instance ID
            unsigned int		m_EAccountType : 4;			// type of account - can't show as EAccountType, due to signed / unsigned difference
            EUniverse			m_EUniverse : 8;	// universe this account belongs to
        } m_comp;

        uint64 m_unAll64Bits;
    } m_steamid;
} CSteamID;

typedef enum EGameIDType
{
    k_EGameIDTypeApp		= 0,
    k_EGameIDTypeGameMod	= 1,
    k_EGameIDTypeShortcut	= 2,
    k_EGameIDTypeP2P		= 3,
} EGameIDType;

typedef struct GameID_t
{
    unsigned int m_nAppID : 24;
    unsigned int m_nType : 8;
    unsigned int m_nModID : 32;
} GameID_t;

typedef struct CGameID {
    union
    {
        uint64 m_ulGameID;
        GameID_t m_gameID;
    };
} CGameID;

typedef struct FriendGameInfo_t
{
    CGameID m_gameID;
    uint32 m_unGameIP;
    uint16 m_usGamePort;
    uint16 m_usQueryPort;
    CSteamID m_steamIDLobby;
} FriendGameInfo_t;

// These values are passed as parameters to the store
typedef enum EOverlayToStoreFlag
{
    k_EOverlayToStoreFlag_None = 0,
    k_EOverlayToStoreFlag_AddToCart = 1,
    k_EOverlayToStoreFlag_AddToCartAndShow = 2,
} EOverlayToStoreFlag;


//-----------------------------------------------------------------------------
// Purpose: Tells Steam where to place the browser window inside the overlay
//-----------------------------------------------------------------------------
typedef enum EActivateGameOverlayToWebPageMode
{
    k_EActivateGameOverlayToWebPageMode_Default = 0,		// Browser will open next to all other windows that the user has open in the overlay.
    // The window will remain open, even if the user closes then re-opens the overlay.

    k_EActivateGameOverlayToWebPageMode_Modal = 1			// Browser will be opened in a special overlay configuration which hides all other windows
    // that the user has open in the overlay. When the user closes the overlay, the browser window
    // will also close. When the user closes the browser window, the overlay will automatically close.
} EActivateGameOverlayToWebPageMode;

//-----------------------------------------------------------------------------
// Purpose: See GetProfileItemPropertyString and GetProfileItemPropertyUint
//-----------------------------------------------------------------------------
typedef enum ECommunityProfileItemType
{
    k_ECommunityProfileItemType_AnimatedAvatar		 = 0,
    k_ECommunityProfileItemType_AvatarFrame			 = 1,
    k_ECommunityProfileItemType_ProfileModifier		 = 2,
    k_ECommunityProfileItemType_ProfileBackground	 = 3,
    k_ECommunityProfileItemType_MiniProfileBackground = 4,
} ECommunityProfileItemType;
typedef enum ECommunityProfileItemProperty
{
    k_ECommunityProfileItemProperty_ImageSmall	   = 0, // string
    k_ECommunityProfileItemProperty_ImageLarge	   = 1, // string
    k_ECommunityProfileItemProperty_InternalName   = 2, // string
    k_ECommunityProfileItemProperty_Title		   = 3, // string
    k_ECommunityProfileItemProperty_Description	   = 4, // string
    k_ECommunityProfileItemProperty_AppID		   = 5, // uint32
    k_ECommunityProfileItemProperty_TypeID		   = 6, // uint32
    k_ECommunityProfileItemProperty_Class		   = 7, // uint32
    k_ECommunityProfileItemProperty_MovieWebM	   = 8, // string
    k_ECommunityProfileItemProperty_MovieMP4	   = 9, // string
    k_ECommunityProfileItemProperty_MovieWebMSmall = 10, // string
    k_ECommunityProfileItemProperty_MovieMP4Small  = 11, // string
} ECommunityProfileItemProperty;

//-----------------------------------------------------------------------------
// Purpose: Chat Entry Types (previously was only friend-to-friend message types)
//-----------------------------------------------------------------------------
typedef enum EChatEntryType
{
    k_EChatEntryTypeInvalid = 0,
    k_EChatEntryTypeChatMsg = 1,		// Normal text message from another user
    k_EChatEntryTypeTyping = 2,			// Another user is typing (not used in multi-user chat)
    k_EChatEntryTypeInviteGame = 3,		// Invite from other user into that users current game
    k_EChatEntryTypeEmote = 4,			// text emote message (deprecated, should be treated as ChatMsg)
    //k_EChatEntryTypeLobbyGameStart = 5,	// lobby game is starting (dead - listen for LobbyGameCreated_t callback instead)
    k_EChatEntryTypeLeftConversation = 6, // user has left the conversation ( closed chat window )
    // Above are previous FriendMsgType entries, now merged into more generic chat entry types
    k_EChatEntryTypeEntered = 7,		// user has entered the conversation (used in multi-user chat and group chat)
    k_EChatEntryTypeWasKicked = 8,		// user was kicked (data: 64-bit steamid of actor performing the kick)
    k_EChatEntryTypeWasBanned = 9,		// user was banned (data: 64-bit steamid of actor performing the ban)
    k_EChatEntryTypeDisconnected = 10,	// user disconnected
    k_EChatEntryTypeHistoricalChat = 11,	// a chat message from user's chat history or offilne message
    //k_EChatEntryTypeReserved1 = 12, // No longer used
    //k_EChatEntryTypeReserved2 = 13, // No longer used
    k_EChatEntryTypeLinkBlocked = 14, // a link was removed by the chat filter.
} EChatEntryType;

//-----------------------------------------------------------------------------
// Purpose: Possible positions to tell the overlay to show notifications in
//-----------------------------------------------------------------------------
typedef enum ENotificationPosition
{
    k_EPositionInvalid = -1,
    k_EPositionTopLeft = 0,
    k_EPositionTopRight = 1,
    k_EPositionBottomLeft = 2,
    k_EPositionBottomRight = 3,
} ENotificationPosition;

// Steam API call failure results
typedef enum ESteamAPICallFailure
{
    k_ESteamAPICallFailureNone = -1,			// no failure
    k_ESteamAPICallFailureSteamGone = 0,		// the local Steam process has gone away
    k_ESteamAPICallFailureNetworkFailure = 1,	// the network connection to Steam has been broken, or was already broken
    // SteamServersDisconnected_t callback will be sent around the same time
    // SteamServersConnected_t will be sent when the client is able to talk to the Steam servers again
    k_ESteamAPICallFailureInvalidHandle = 2,	// the SteamAPICall_t handle passed in no longer exists
    k_ESteamAPICallFailureMismatchedCallback = 3,// GetAPICallResult() was called with the wrong callback type for this API call
} ESteamAPICallFailure;

// Input modes for the Big Picture gamepad text entry
typedef enum EGamepadTextInputMode
{
    k_EGamepadTextInputModeNormal = 0,
    k_EGamepadTextInputModePassword = 1
} EGamepadTextInputMode;

// Controls number of allowed lines for the Big Picture gamepad text entry
typedef enum EGamepadTextInputLineMode
{
    k_EGamepadTextInputLineModeSingleLine = 0,
    k_EGamepadTextInputLineModeMultipleLines = 1
} EGamepadTextInputLineMode;

typedef enum EFloatingGamepadTextInputMode
{
    k_EFloatingGamepadTextInputModeModeSingleLine = 0,		// Enter dismisses the keyboard
    k_EFloatingGamepadTextInputModeModeMultipleLines = 1,	// User needs to explictly close the keyboard
    k_EFloatingGamepadTextInputModeModeEmail = 2,			// Keyboard layout is email, enter dismisses the keyboard
    k_EFloatingGamepadTextInputModeModeNumeric = 3,			// Keyboard layout is numeric, enter dismisses the keyboard

} EFloatingGamepadTextInputMode;

// The context where text filtering is being done
typedef enum ETextFilteringContext
{
    k_ETextFilteringContextUnknown = 0,	// Unknown context
    k_ETextFilteringContextGameContent = 1,	// Game content, only legally required filtering is performed
    k_ETextFilteringContextChat = 2,	// Chat from another player
    k_ETextFilteringContextName = 3,	// Character or item name
} ETextFilteringContext;

typedef enum EGameSearchErrorCode_t
{
    k_EGameSearchErrorCode_OK = 1,
    k_EGameSearchErrorCode_Failed_Search_Already_In_Progress = 2,
    k_EGameSearchErrorCode_Failed_No_Search_In_Progress = 3,
    k_EGameSearchErrorCode_Failed_Not_Lobby_Leader = 4, // if not the lobby leader can not call SearchForGameWithLobby
    k_EGameSearchErrorCode_Failed_No_Host_Available = 5, // no host is available that matches those search params
    k_EGameSearchErrorCode_Failed_Search_Params_Invalid = 6, // search params are invalid
    k_EGameSearchErrorCode_Failed_Offline = 7, // offline, could not communicate with server
    k_EGameSearchErrorCode_Failed_NotAuthorized = 8, // either the user or the application does not have priveledges to do this
    k_EGameSearchErrorCode_Failed_Unknown_Error = 9, // unknown error
} EGameSearchErrorCode_t;

typedef enum EPlayerResult_t
{
    k_EPlayerResultFailedToConnect = 1, // failed to connect after confirming
    k_EPlayerResultAbandoned = 2,		// quit game without completing it
    k_EPlayerResultKicked = 3,			// kicked by other players/moderator/server rules
    k_EPlayerResultIncomplete = 4,		// player stayed to end but game did not conclude successfully ( nofault to player )
    k_EPlayerResultCompleted = 5,		// player completed game
} EPlayerResult_t;

typedef enum ESteamIPv6ConnectivityProtocol
{
    k_ESteamIPv6ConnectivityProtocol_Invalid = 0,
    k_ESteamIPv6ConnectivityProtocol_HTTP = 1,		// because a proxy may make this different than other protocols
    k_ESteamIPv6ConnectivityProtocol_UDP = 2,		// test UDP connectivity. Uses a port that is commonly needed for other Steam stuff. If UDP works, TCP probably works.
} ESteamIPv6ConnectivityProtocol;

// For the above transport protocol, what do we think the local machine's connectivity to the internet over ipv6 is like
typedef enum ESteamIPv6ConnectivityState
{
    k_ESteamIPv6ConnectivityState_Unknown = 0,	// We haven't run a test yet
    k_ESteamIPv6ConnectivityState_Good = 1,		// We have recently been able to make a request on ipv6 for the given protocol
    k_ESteamIPv6ConnectivityState_Bad = 2,		// We failed to make a request, either because this machine has no ipv6 address assigned, or it has no upstream connectivity
} ESteamIPv6ConnectivityState;

// lobby type description
typedef enum ELobbyType
{
    k_ELobbyTypePrivate = 0,		// only way to join the lobby is to invite to someone else
    k_ELobbyTypeFriendsOnly = 1,	// shows for friends or invitees, but not in lobby list
    k_ELobbyTypePublic = 2,			// visible for friends and in lobby list
    k_ELobbyTypeInvisible = 3,		// returned by search, but not visible to other friends
    //    useful if you want a user in two lobbies, for example matching groups together
    //	  a user can be in only one regular lobby, and up to two invisible lobbies
    k_ELobbyTypePrivateUnique = 4,	// private, unique and does not delete when empty - only one of these may exist per unique keypair set
    // can only create from webapi
} ELobbyType;

// lobby search filter tools
typedef enum ELobbyComparison
{
    k_ELobbyComparisonEqualToOrLessThan = -2,
    k_ELobbyComparisonLessThan = -1,
    k_ELobbyComparisonEqual = 0,
    k_ELobbyComparisonGreaterThan = 1,
    k_ELobbyComparisonEqualToOrGreaterThan = 2,
    k_ELobbyComparisonNotEqual = 3,
} ELobbyComparison;

// lobby search distance. Lobby results are sorted from closest to farthest.
typedef enum ELobbyDistanceFilter
{
    k_ELobbyDistanceFilterClose,		// only lobbies in the same immediate region will be returned
    k_ELobbyDistanceFilterDefault,		// only lobbies in the same region or near by regions
    k_ELobbyDistanceFilterFar,			// for games that don't have many latency requirements, will return lobbies about half-way around the globe
    k_ELobbyDistanceFilterWorldwide,	// no filtering, will match lobbies as far as India to NY (not recommended, expect multiple seconds of latency between the clients)
} ELobbyDistanceFilter;

typedef void* HServerListRequest;
typedef int HServerQuery;
const int HSERVERQUERY_INVALID = 0xffffffff;

typedef enum EMatchMakingServerResponse
{
    eServerResponded = 0,
    eServerFailedToRespond,
    eNoServersListedOnMasterServer // for the Internet query type, returned in response callback if no servers of this type match
} EMatchMakingServerResponse;

typedef enum ESteamPartyBeaconLocationType
{
    k_ESteamPartyBeaconLocationType_Invalid = 0,
    k_ESteamPartyBeaconLocationType_ChatGroup = 1,

    k_ESteamPartyBeaconLocationType_Max,
} ESteamPartyBeaconLocationType;

typedef struct SteamPartyBeaconLocation_t
{
    ESteamPartyBeaconLocationType m_eType;
    uint64 m_ulLocationID;
} SteamPartyBeaconLocation_t;


typedef enum ESteamPartyBeaconLocationData
{
    k_ESteamPartyBeaconLocationDataInvalid = 0,
    k_ESteamPartyBeaconLocationDataName = 1,
    k_ESteamPartyBeaconLocationDataIconURLSmall = 2,
    k_ESteamPartyBeaconLocationDataIconURLMedium = 3,
    k_ESteamPartyBeaconLocationDataIconURLLarge = 4,
} ESteamPartyBeaconLocationData;

typedef struct SteamParamStringArray_t
{
    const char ** m_ppStrings;
    int32 m_nNumStrings;
} SteamParamStringArray_t;

// A handle to a piece of user generated content
typedef uint64 UGCHandle_t;
typedef uint64 PublishedFileUpdateHandle_t;
typedef uint64 PublishedFileId_t;
const PublishedFileId_t k_PublishedFileIdInvalid = 0;
const UGCHandle_t k_UGCHandleInvalid = 0xffffffffffffffffull;
const PublishedFileUpdateHandle_t k_PublishedFileUpdateHandleInvalid = 0xffffffffffffffffull;

// Handle for writing to Steam Cloud
typedef uint64 UGCFileWriteStreamHandle_t;
const UGCFileWriteStreamHandle_t k_UGCFileStreamHandleInvalid = 0xffffffffffffffffull;

typedef enum ERemoteStoragePlatform
{
    k_ERemoteStoragePlatformNone		= 0,
    k_ERemoteStoragePlatformWindows		= (1 << 0),
    k_ERemoteStoragePlatformOSX			= (1 << 1),
    k_ERemoteStoragePlatformPS3			= (1 << 2),
    k_ERemoteStoragePlatformLinux		= (1 << 3),
    k_ERemoteStoragePlatformSwitch		= (1 << 4),
    k_ERemoteStoragePlatformAndroid		= (1 << 5),
    k_ERemoteStoragePlatformIOS			= (1 << 6),
    // NB we get one more before we need to widen some things

    k_ERemoteStoragePlatformAll = 0xffffffff
} ERemoteStoragePlatform;

typedef enum ERemoteStoragePublishedFileVisibility
{
    k_ERemoteStoragePublishedFileVisibilityPublic = 0,
    k_ERemoteStoragePublishedFileVisibilityFriendsOnly = 1,
    k_ERemoteStoragePublishedFileVisibilityPrivate = 2,
    k_ERemoteStoragePublishedFileVisibilityUnlisted = 3,
} ERemoteStoragePublishedFileVisibility;


typedef enum EWorkshopFileType
{
    k_EWorkshopFileTypeFirst = 0,

    k_EWorkshopFileTypeCommunity			  = 0,		// normal Workshop item that can be subscribed to
    k_EWorkshopFileTypeMicrotransaction		  = 1,		// Workshop item that is meant to be voted on for the purpose of selling in-game
    k_EWorkshopFileTypeCollection			  = 2,		// a collection of Workshop or Greenlight items
    k_EWorkshopFileTypeArt					  = 3,		// artwork
    k_EWorkshopFileTypeVideo				  = 4,		// external video
    k_EWorkshopFileTypeScreenshot			  = 5,		// screenshot
    k_EWorkshopFileTypeGame					  = 6,		// Greenlight game entry
    k_EWorkshopFileTypeSoftware				  = 7,		// Greenlight software entry
    k_EWorkshopFileTypeConcept				  = 8,		// Greenlight concept
    k_EWorkshopFileTypeWebGuide				  = 9,		// Steam web guide
    k_EWorkshopFileTypeIntegratedGuide		  = 10,		// application integrated guide
    k_EWorkshopFileTypeMerch				  = 11,		// Workshop merchandise meant to be voted on for the purpose of being sold
    k_EWorkshopFileTypeControllerBinding	  = 12,		// Steam Controller bindings
    k_EWorkshopFileTypeSteamworksAccessInvite = 13,		// internal
    k_EWorkshopFileTypeSteamVideo			  = 14,		// Steam video
    k_EWorkshopFileTypeGameManagedItem		  = 15,		// managed completely by the game, not the user, and not shown on the web
    k_EWorkshopFileTypeClip					  = 16,		// internal

    // Update k_EWorkshopFileTypeMax if you add values.
    k_EWorkshopFileTypeMax = 17

} EWorkshopFileType;

typedef enum EWorkshopVote
{
    k_EWorkshopVoteUnvoted = 0,
    k_EWorkshopVoteFor = 1,
    k_EWorkshopVoteAgainst = 2,
    k_EWorkshopVoteLater = 3,
} EWorkshopVote;

typedef enum EWorkshopFileAction
{
    k_EWorkshopFileActionPlayed = 0,
    k_EWorkshopFileActionCompleted = 1,
} EWorkshopFileAction;

typedef enum EWorkshopEnumerationType
{
    k_EWorkshopEnumerationTypeRankedByVote = 0,
    k_EWorkshopEnumerationTypeRecent = 1,
    k_EWorkshopEnumerationTypeTrending = 2,
    k_EWorkshopEnumerationTypeFavoritesOfFriends = 3,
    k_EWorkshopEnumerationTypeVotedByFriends = 4,
    k_EWorkshopEnumerationTypeContentByFriends = 5,
    k_EWorkshopEnumerationTypeRecentFromFollowedUsers = 6,
} EWorkshopEnumerationType;

typedef enum EWorkshopVideoProvider
{
    k_EWorkshopVideoProviderNone = 0,
    k_EWorkshopVideoProviderYoutube = 1
} EWorkshopVideoProvider;


typedef enum EUGCReadAction
{
    // Keeps the file handle open unless the last byte is read.  You can use this when reading large files (over 100MB) in sequential chunks.
    // If the last byte is read, this will behave the same as k_EUGCRead_Close.  Otherwise, it behaves the same as k_EUGCRead_ContinueReading.
    // This value maintains the same behavior as before the EUGCReadAction parameter was introduced.
    k_EUGCRead_ContinueReadingUntilFinished = 0,

    // Keeps the file handle open.  Use this when using UGCRead to seek to different parts of the file.
    // When you are done seeking around the file, make a final call with k_EUGCRead_Close to close it.
    k_EUGCRead_ContinueReading = 1,

    // Frees the file handle.  Use this when you're done reading the content.
    // To read the file from Steam again you will need to call UGCDownload again.
    k_EUGCRead_Close = 2,
} EUGCReadAction;

typedef enum ERemoteStorageLocalFileChange
{
    k_ERemoteStorageLocalFileChange_Invalid = 0,

    // The file was updated from another device
    k_ERemoteStorageLocalFileChange_FileUpdated = 1,

    // The file was deleted by another device
    k_ERemoteStorageLocalFileChange_FileDeleted = 2,
} ERemoteStorageLocalFileChange;

typedef enum ERemoteStorageFilePathType
{
    k_ERemoteStorageFilePathType_Invalid = 0,

    // The file is directly accessed by the game and this is the full path
    k_ERemoteStorageFilePathType_Absolute = 1,

    // The file is accessed via the ISteamRemoteStorage API and this is the filename
    k_ERemoteStorageFilePathType_APIFilename = 2,
} ERemoteStorageFilePathType;

// handle to a single leaderboard
typedef uint64 SteamLeaderboard_t;

// handle to a set of downloaded entries in a leaderboard
typedef uint64 SteamLeaderboardEntries_t;

// type of data request, when downloading leaderboard entries
typedef enum ELeaderboardDataRequest
{
    k_ELeaderboardDataRequestGlobal = 0,
    k_ELeaderboardDataRequestGlobalAroundUser = 1,
    k_ELeaderboardDataRequestFriends = 2,
    k_ELeaderboardDataRequestUsers = 3
} ELeaderboardDataRequest;

// the sort order of a leaderboard
typedef enum ELeaderboardSortMethod
{
    k_ELeaderboardSortMethodNone = 0,
    k_ELeaderboardSortMethodAscending = 1,	// top-score is lowest number
    k_ELeaderboardSortMethodDescending = 2,	// top-score is highest number
} ELeaderboardSortMethod;

// the display type (used by the Steam Community web site) for a leaderboard
typedef enum ELeaderboardDisplayType
{
    k_ELeaderboardDisplayTypeNone = 0,
    k_ELeaderboardDisplayTypeNumeric = 1,			// simple numerical score
    k_ELeaderboardDisplayTypeTimeSeconds = 2,		// the score represents a time, in seconds
    k_ELeaderboardDisplayTypeTimeMilliSeconds = 3,	// the score represents a time, in milliseconds
} ELeaderboardDisplayType;

typedef enum ELeaderboardUploadScoreMethod
{
    k_ELeaderboardUploadScoreMethodNone = 0,
    k_ELeaderboardUploadScoreMethodKeepBest = 1,	// Leaderboard will keep user's best score
    k_ELeaderboardUploadScoreMethodForceUpdate = 2,	// Leaderboard will always replace score with specified
} ELeaderboardUploadScoreMethod;

typedef struct LeaderboardEntry_t
{
    CSteamID m_steamIDUser; // user with the entry - use SteamFriends()->GetFriendPersonaName() & SteamFriends()->GetFriendAvatar() to get more info
    int32 m_nGlobalRank;	// [1..N], where N is the number of users with an entry in the leaderboard
    int32 m_nScore;			// score as set in the leaderboard
    int32 m_cDetails;		// number of int32 details available for this entry
    UGCHandle_t m_hUGC;		// handle for UGC attached to the entry
} LeaderboardEntry_t;

// list of possible errors returned by SendP2PPacket() API
// these will be posted in the P2PSessionConnectFail_t callback
typedef enum EP2PSessionError
{
    k_EP2PSessionErrorNone = 0,
    k_EP2PSessionErrorNoRightsToApp = 2,			// local user doesn't own the app that is running
    k_EP2PSessionErrorTimeout = 4,					// target isn't responding, perhaps not calling AcceptP2PSessionWithUser()
    // corporate firewalls can also block this (NAT traversal is not firewall traversal)
    // make sure that UDP ports 3478, 4379, and 4380 are open in an outbound direction

    // The following error codes were removed and will never be sent.
    // For privacy reasons, there is no reply if the user is offline or playing another game.
    k_EP2PSessionErrorNotRunningApp_DELETED = 1,
    k_EP2PSessionErrorDestinationNotLoggedIn_DELETED = 3,

    k_EP2PSessionErrorMax = 5
} EP2PSessionError;

// SendP2PPacket() send types
// Typically k_EP2PSendUnreliable is what you want for UDP-like packets, k_EP2PSendReliable for TCP-like packets
typedef enum EP2PSend
{
    // Basic UDP send. Packets can't be bigger than 1200 bytes (your typical MTU size). Can be lost, or arrive out of order (rare).
    // The sending API does have some knowledge of the underlying connection, so if there is no NAT-traversal accomplished or
    // there is a recognized adjustment happening on the connection, the packet will be batched until the connection is open again.
    k_EP2PSendUnreliable = 0,

    // As above, but if the underlying p2p connection isn't yet established the packet will just be thrown away. Using this on the first
    // packet sent to a remote host almost guarantees the packet will be dropped.
    // This is only really useful for kinds of data that should never buffer up, i.e. voice payload packets
    k_EP2PSendUnreliableNoDelay = 1,

    // Reliable message send. Can send up to 1MB of data in a single message.
    // Does fragmentation/re-assembly of messages under the hood, as well as a sliding window for efficient sends of large chunks of data.
    k_EP2PSendReliable = 2,

    // As above, but applies the Nagle algorithm to the send - sends will accumulate
    // until the current MTU size (typically ~1200 bytes, but can change) or ~200ms has passed (Nagle algorithm).
    // Useful if you want to send a set of smaller messages but have the coalesced into a single packet
    // Since the reliable stream is all ordered, you can do several small message sends with k_EP2PSendReliableWithBuffering and then
    // do a normal k_EP2PSendReliable to force all the buffered data to be sent.
    k_EP2PSendReliableWithBuffering = 3,

} EP2PSend;

typedef struct P2PSessionState_t
{
    uint8 m_bConnectionActive;		// true if we've got an active open connection
    uint8 m_bConnecting;			// true if we're currently trying to establish a connection
    uint8 m_eP2PSessionError;		// last error recorded (see enum above)
    uint8 m_bUsingRelay;			// true if it's going through a relay server (TURN)
    int32 m_nBytesQueuedForSend;
    int32 m_nPacketsQueuedForSend;
    uint32 m_nRemoteIP;				// potential IP:Port of remote host. Could be TURN server.
    uint16 m_nRemotePort;			// Only exists for compatibility with older authentication api's
} P2PSessionState_t;

// handle to a socket
typedef uint32 SNetSocket_t;		// CreateP2PConnectionSocket()
typedef uint32 SNetListenSocket_t;	// CreateListenSocket()

// connection progress indicators, used by CreateP2PConnectionSocket()
typedef enum ESNetSocketState
{
    k_ESNetSocketStateInvalid = 0,

    // communication is valid
    k_ESNetSocketStateConnected = 1,

    // states while establishing a connection
    k_ESNetSocketStateInitiated = 10,				// the connection state machine has started

    // p2p connections
    k_ESNetSocketStateLocalCandidatesFound = 11,	// we've found our local IP info
    k_ESNetSocketStateReceivedRemoteCandidates = 12,// we've received information from the remote machine, via the Steam back-end, about their IP info

    // direct connections
    k_ESNetSocketStateChallengeHandshake = 15,		// we've received a challenge packet from the server

    // failure states
    k_ESNetSocketStateDisconnecting = 21,			// the API shut it down, and we're in the process of telling the other end
    k_ESNetSocketStateLocalDisconnect = 22,			// the API shut it down, and we've completed shutdown
    k_ESNetSocketStateTimeoutDuringConnect = 23,	// we timed out while trying to creating the connection
    k_ESNetSocketStateRemoteEndDisconnected = 24,	// the remote end has disconnected from us
    k_ESNetSocketStateConnectionBroken = 25,		// connection has been broken; either the other end has disappeared or our local network connection has broke

} ESNetSocketState;

// describes how the socket is currently connected
typedef enum ESNetSocketConnectionType
{
    k_ESNetSocketConnectionTypeNotConnected = 0,
    k_ESNetSocketConnectionTypeUDP = 1,
    k_ESNetSocketConnectionTypeUDPRelay = 2,
} ESNetSocketConnectionType;

// Handle is valid for the lifetime of your process and no longer
typedef uint32 ScreenshotHandle;
#define INVALID_SCREENSHOT_HANDLE 0

typedef enum EVRScreenshotType
{
    k_EVRScreenshotType_None			= 0,
    k_EVRScreenshotType_Mono			= 1,
    k_EVRScreenshotType_Stereo			= 2,
    k_EVRScreenshotType_MonoCubemap		= 3,
    k_EVRScreenshotType_MonoPanorama	= 4,
    k_EVRScreenshotType_StereoPanorama	= 5
} EVRScreenshotType;

typedef enum AudioPlayback_Status
{
    AudioPlayback_Undefined = 0,
    AudioPlayback_Playing = 1,
    AudioPlayback_Paused = 2,
    AudioPlayback_Idle = 3
} AudioPlayback_Status;

// Handle to a HTTP Request handle
typedef uint32 HTTPRequestHandle;
#define INVALID_HTTPREQUEST_HANDLE		0

typedef uint32 HTTPCookieContainerHandle;
#define INVALID_HTTPCOOKIE_HANDLE		0
// This enum is used in client API methods, do not re-number existing values.
typedef enum EHTTPMethod
{
    k_EHTTPMethodInvalid = 0,
    k_EHTTPMethodGET,
    k_EHTTPMethodHEAD,
    k_EHTTPMethodPOST,
    k_EHTTPMethodPUT,
    k_EHTTPMethodDELETE,
    k_EHTTPMethodOPTIONS,
    k_EHTTPMethodPATCH,

    // The remaining HTTP methods are not yet supported, per rfc2616 section 5.1.1 only GET and HEAD are required for
    // a compliant general purpose server.  We'll likely add more as we find uses for them.

    // k_EHTTPMethodTRACE,
    // k_EHTTPMethodCONNECT
} EHTTPMethod;

// HTTP Status codes that the server can send in response to a request, see rfc2616 section 10.3 for descriptions
// of each of these.
typedef enum EHTTPStatusCode
{
    // Invalid status code (this isn't defined in HTTP, used to indicate unset in our code)
    k_EHTTPStatusCodeInvalid =					0,

    // Informational codes
    k_EHTTPStatusCode100Continue =				100,
    k_EHTTPStatusCode101SwitchingProtocols =	101,

    // Success codes
    k_EHTTPStatusCode200OK =					200,
    k_EHTTPStatusCode201Created =				201,
    k_EHTTPStatusCode202Accepted =				202,
    k_EHTTPStatusCode203NonAuthoritative =		203,
    k_EHTTPStatusCode204NoContent =				204,
    k_EHTTPStatusCode205ResetContent =			205,
    k_EHTTPStatusCode206PartialContent =		206,

    // Redirection codes
    k_EHTTPStatusCode300MultipleChoices =		300,
    k_EHTTPStatusCode301MovedPermanently =		301,
    k_EHTTPStatusCode302Found =					302,
    k_EHTTPStatusCode303SeeOther =				303,
    k_EHTTPStatusCode304NotModified =			304,
    k_EHTTPStatusCode305UseProxy =				305,
    //k_EHTTPStatusCode306Unused =				306, (used in old HTTP spec, now unused in 1.1)
    k_EHTTPStatusCode307TemporaryRedirect =		307,
    k_EHTTPStatusCode308PermanentRedirect =		308,

    // Error codes
    k_EHTTPStatusCode400BadRequest =			400,
    k_EHTTPStatusCode401Unauthorized =			401, // You probably want 403 or something else. 401 implies you're sending a WWW-Authenticate header and the client can sent an Authorization header in response.
    k_EHTTPStatusCode402PaymentRequired =		402, // This is reserved for future HTTP specs, not really supported by clients
    k_EHTTPStatusCode403Forbidden =				403,
    k_EHTTPStatusCode404NotFound =				404,
    k_EHTTPStatusCode405MethodNotAllowed =		405,
    k_EHTTPStatusCode406NotAcceptable =			406,
    k_EHTTPStatusCode407ProxyAuthRequired =		407,
    k_EHTTPStatusCode408RequestTimeout =		408,
    k_EHTTPStatusCode409Conflict =				409,
    k_EHTTPStatusCode410Gone =					410,
    k_EHTTPStatusCode411LengthRequired =		411,
    k_EHTTPStatusCode412PreconditionFailed =	412,
    k_EHTTPStatusCode413RequestEntityTooLarge =	413,
    k_EHTTPStatusCode414RequestURITooLong =		414,
    k_EHTTPStatusCode415UnsupportedMediaType =	415,
    k_EHTTPStatusCode416RequestedRangeNotSatisfiable = 416,
    k_EHTTPStatusCode417ExpectationFailed =		417,
    k_EHTTPStatusCode4xxUnknown = 				418, // 418 is reserved, so we'll use it to mean unknown
    k_EHTTPStatusCode429TooManyRequests	=		429,
    k_EHTTPStatusCode444ConnectionClosed =		444, // nginx only?

    // Server error codes
    k_EHTTPStatusCode500InternalServerError =	500,
    k_EHTTPStatusCode501NotImplemented =		501,
    k_EHTTPStatusCode502BadGateway =			502,
    k_EHTTPStatusCode503ServiceUnavailable =	503,
    k_EHTTPStatusCode504GatewayTimeout =		504,
    k_EHTTPStatusCode505HTTPVersionNotSupported = 505,
    k_EHTTPStatusCode5xxUnknown =				599,
} EHTTPStatusCode;

#define STEAM_INPUT_MAX_COUNT 16

#define STEAM_INPUT_MAX_ANALOG_ACTIONS 24

#define STEAM_INPUT_MAX_DIGITAL_ACTIONS 256

#define STEAM_INPUT_MAX_ORIGINS 8

#define STEAM_INPUT_MAX_ACTIVE_LAYERS 16

// When sending an option to a specific controller handle, you can send to all devices via this command
#define STEAM_INPUT_HANDLE_ALL_CONTROLLERS UINT64_MAX

#define STEAM_INPUT_MIN_ANALOG_ACTION_DATA -1.0f
#define STEAM_INPUT_MAX_ANALOG_ACTION_DATA 1.0f

typedef enum EInputSourceMode
{
    k_EInputSourceMode_None,
    k_EInputSourceMode_Dpad,
    k_EInputSourceMode_Buttons,
    k_EInputSourceMode_FourButtons,
    k_EInputSourceMode_AbsoluteMouse,
    k_EInputSourceMode_RelativeMouse,
    k_EInputSourceMode_JoystickMove,
    k_EInputSourceMode_JoystickMouse,
    k_EInputSourceMode_JoystickCamera,
    k_EInputSourceMode_ScrollWheel,
    k_EInputSourceMode_Trigger,
    k_EInputSourceMode_TouchMenu,
    k_EInputSourceMode_MouseJoystick,
    k_EInputSourceMode_MouseRegion,
    k_EInputSourceMode_RadialMenu,
    k_EInputSourceMode_SingleButton,
    k_EInputSourceMode_Switches
} EInputSourceMode;

// Note: Please do not use action origins as a way to identify controller types. There is no
// guarantee that they will be added in a contiguous manner - use GetInputTypeForHandle instead.
// Versions of Steam that add new controller types in the future will extend this enum so if you're
// using a lookup table please check the bounds of any origins returned by Steam.
typedef enum EInputActionOrigin
{
    // Steam Controller
    k_EInputActionOrigin_None,
    k_EInputActionOrigin_SteamController_A,
    k_EInputActionOrigin_SteamController_B,
    k_EInputActionOrigin_SteamController_X,
    k_EInputActionOrigin_SteamController_Y,
    k_EInputActionOrigin_SteamController_LeftBumper,
    k_EInputActionOrigin_SteamController_RightBumper,
    k_EInputActionOrigin_SteamController_LeftGrip,
    k_EInputActionOrigin_SteamController_RightGrip,
    k_EInputActionOrigin_SteamController_Start,
    k_EInputActionOrigin_SteamController_Back,
    k_EInputActionOrigin_SteamController_LeftPad_Touch,
    k_EInputActionOrigin_SteamController_LeftPad_Swipe,
    k_EInputActionOrigin_SteamController_LeftPad_Click,
    k_EInputActionOrigin_SteamController_LeftPad_DPadNorth,
    k_EInputActionOrigin_SteamController_LeftPad_DPadSouth,
    k_EInputActionOrigin_SteamController_LeftPad_DPadWest,
    k_EInputActionOrigin_SteamController_LeftPad_DPadEast,
    k_EInputActionOrigin_SteamController_RightPad_Touch,
    k_EInputActionOrigin_SteamController_RightPad_Swipe,
    k_EInputActionOrigin_SteamController_RightPad_Click,
    k_EInputActionOrigin_SteamController_RightPad_DPadNorth,
    k_EInputActionOrigin_SteamController_RightPad_DPadSouth,
    k_EInputActionOrigin_SteamController_RightPad_DPadWest,
    k_EInputActionOrigin_SteamController_RightPad_DPadEast,
    k_EInputActionOrigin_SteamController_LeftTrigger_Pull,
    k_EInputActionOrigin_SteamController_LeftTrigger_Click,
    k_EInputActionOrigin_SteamController_RightTrigger_Pull,
    k_EInputActionOrigin_SteamController_RightTrigger_Click,
    k_EInputActionOrigin_SteamController_LeftStick_Move,
    k_EInputActionOrigin_SteamController_LeftStick_Click,
    k_EInputActionOrigin_SteamController_LeftStick_DPadNorth,
    k_EInputActionOrigin_SteamController_LeftStick_DPadSouth,
    k_EInputActionOrigin_SteamController_LeftStick_DPadWest,
    k_EInputActionOrigin_SteamController_LeftStick_DPadEast,
    k_EInputActionOrigin_SteamController_Gyro_Move,
    k_EInputActionOrigin_SteamController_Gyro_Pitch,
    k_EInputActionOrigin_SteamController_Gyro_Yaw,
    k_EInputActionOrigin_SteamController_Gyro_Roll,
    k_EInputActionOrigin_SteamController_Reserved0,
    k_EInputActionOrigin_SteamController_Reserved1,
    k_EInputActionOrigin_SteamController_Reserved2,
    k_EInputActionOrigin_SteamController_Reserved3,
    k_EInputActionOrigin_SteamController_Reserved4,
    k_EInputActionOrigin_SteamController_Reserved5,
    k_EInputActionOrigin_SteamController_Reserved6,
    k_EInputActionOrigin_SteamController_Reserved7,
    k_EInputActionOrigin_SteamController_Reserved8,
    k_EInputActionOrigin_SteamController_Reserved9,
    k_EInputActionOrigin_SteamController_Reserved10,

    // PS4 Dual Shock
    k_EInputActionOrigin_PS4_X,
    k_EInputActionOrigin_PS4_Circle,
    k_EInputActionOrigin_PS4_Triangle,
    k_EInputActionOrigin_PS4_Square,
    k_EInputActionOrigin_PS4_LeftBumper,
    k_EInputActionOrigin_PS4_RightBumper,
    k_EInputActionOrigin_PS4_Options,	//Start
    k_EInputActionOrigin_PS4_Share,		//Back
    k_EInputActionOrigin_PS4_LeftPad_Touch,
    k_EInputActionOrigin_PS4_LeftPad_Swipe,
    k_EInputActionOrigin_PS4_LeftPad_Click,
    k_EInputActionOrigin_PS4_LeftPad_DPadNorth,
    k_EInputActionOrigin_PS4_LeftPad_DPadSouth,
    k_EInputActionOrigin_PS4_LeftPad_DPadWest,
    k_EInputActionOrigin_PS4_LeftPad_DPadEast,
    k_EInputActionOrigin_PS4_RightPad_Touch,
    k_EInputActionOrigin_PS4_RightPad_Swipe,
    k_EInputActionOrigin_PS4_RightPad_Click,
    k_EInputActionOrigin_PS4_RightPad_DPadNorth,
    k_EInputActionOrigin_PS4_RightPad_DPadSouth,
    k_EInputActionOrigin_PS4_RightPad_DPadWest,
    k_EInputActionOrigin_PS4_RightPad_DPadEast,
    k_EInputActionOrigin_PS4_CenterPad_Touch,
    k_EInputActionOrigin_PS4_CenterPad_Swipe,
    k_EInputActionOrigin_PS4_CenterPad_Click,
    k_EInputActionOrigin_PS4_CenterPad_DPadNorth,
    k_EInputActionOrigin_PS4_CenterPad_DPadSouth,
    k_EInputActionOrigin_PS4_CenterPad_DPadWest,
    k_EInputActionOrigin_PS4_CenterPad_DPadEast,
    k_EInputActionOrigin_PS4_LeftTrigger_Pull,
    k_EInputActionOrigin_PS4_LeftTrigger_Click,
    k_EInputActionOrigin_PS4_RightTrigger_Pull,
    k_EInputActionOrigin_PS4_RightTrigger_Click,
    k_EInputActionOrigin_PS4_LeftStick_Move,
    k_EInputActionOrigin_PS4_LeftStick_Click,
    k_EInputActionOrigin_PS4_LeftStick_DPadNorth,
    k_EInputActionOrigin_PS4_LeftStick_DPadSouth,
    k_EInputActionOrigin_PS4_LeftStick_DPadWest,
    k_EInputActionOrigin_PS4_LeftStick_DPadEast,
    k_EInputActionOrigin_PS4_RightStick_Move,
    k_EInputActionOrigin_PS4_RightStick_Click,
    k_EInputActionOrigin_PS4_RightStick_DPadNorth,
    k_EInputActionOrigin_PS4_RightStick_DPadSouth,
    k_EInputActionOrigin_PS4_RightStick_DPadWest,
    k_EInputActionOrigin_PS4_RightStick_DPadEast,
    k_EInputActionOrigin_PS4_DPad_North,
    k_EInputActionOrigin_PS4_DPad_South,
    k_EInputActionOrigin_PS4_DPad_West,
    k_EInputActionOrigin_PS4_DPad_East,
    k_EInputActionOrigin_PS4_Gyro_Move,
    k_EInputActionOrigin_PS4_Gyro_Pitch,
    k_EInputActionOrigin_PS4_Gyro_Yaw,
    k_EInputActionOrigin_PS4_Gyro_Roll,
    k_EInputActionOrigin_PS4_DPad_Move,
    k_EInputActionOrigin_PS4_Reserved1,
    k_EInputActionOrigin_PS4_Reserved2,
    k_EInputActionOrigin_PS4_Reserved3,
    k_EInputActionOrigin_PS4_Reserved4,
    k_EInputActionOrigin_PS4_Reserved5,
    k_EInputActionOrigin_PS4_Reserved6,
    k_EInputActionOrigin_PS4_Reserved7,
    k_EInputActionOrigin_PS4_Reserved8,
    k_EInputActionOrigin_PS4_Reserved9,
    k_EInputActionOrigin_PS4_Reserved10,

    // XBox One
    k_EInputActionOrigin_XBoxOne_A,
    k_EInputActionOrigin_XBoxOne_B,
    k_EInputActionOrigin_XBoxOne_X,
    k_EInputActionOrigin_XBoxOne_Y,
    k_EInputActionOrigin_XBoxOne_LeftBumper,
    k_EInputActionOrigin_XBoxOne_RightBumper,
    k_EInputActionOrigin_XBoxOne_Menu,  //Start
    k_EInputActionOrigin_XBoxOne_View,  //Back
    k_EInputActionOrigin_XBoxOne_LeftTrigger_Pull,
    k_EInputActionOrigin_XBoxOne_LeftTrigger_Click,
    k_EInputActionOrigin_XBoxOne_RightTrigger_Pull,
    k_EInputActionOrigin_XBoxOne_RightTrigger_Click,
    k_EInputActionOrigin_XBoxOne_LeftStick_Move,
    k_EInputActionOrigin_XBoxOne_LeftStick_Click,
    k_EInputActionOrigin_XBoxOne_LeftStick_DPadNorth,
    k_EInputActionOrigin_XBoxOne_LeftStick_DPadSouth,
    k_EInputActionOrigin_XBoxOne_LeftStick_DPadWest,
    k_EInputActionOrigin_XBoxOne_LeftStick_DPadEast,
    k_EInputActionOrigin_XBoxOne_RightStick_Move,
    k_EInputActionOrigin_XBoxOne_RightStick_Click,
    k_EInputActionOrigin_XBoxOne_RightStick_DPadNorth,
    k_EInputActionOrigin_XBoxOne_RightStick_DPadSouth,
    k_EInputActionOrigin_XBoxOne_RightStick_DPadWest,
    k_EInputActionOrigin_XBoxOne_RightStick_DPadEast,
    k_EInputActionOrigin_XBoxOne_DPad_North,
    k_EInputActionOrigin_XBoxOne_DPad_South,
    k_EInputActionOrigin_XBoxOne_DPad_West,
    k_EInputActionOrigin_XBoxOne_DPad_East,
    k_EInputActionOrigin_XBoxOne_DPad_Move,
    k_EInputActionOrigin_XBoxOne_LeftGrip_Lower,
    k_EInputActionOrigin_XBoxOne_LeftGrip_Upper,
    k_EInputActionOrigin_XBoxOne_RightGrip_Lower,
    k_EInputActionOrigin_XBoxOne_RightGrip_Upper,
    k_EInputActionOrigin_XBoxOne_Share, // Xbox Series X controllers only
    k_EInputActionOrigin_XBoxOne_Reserved6,
    k_EInputActionOrigin_XBoxOne_Reserved7,
    k_EInputActionOrigin_XBoxOne_Reserved8,
    k_EInputActionOrigin_XBoxOne_Reserved9,
    k_EInputActionOrigin_XBoxOne_Reserved10,

    // XBox 360
    k_EInputActionOrigin_XBox360_A,
    k_EInputActionOrigin_XBox360_B,
    k_EInputActionOrigin_XBox360_X,
    k_EInputActionOrigin_XBox360_Y,
    k_EInputActionOrigin_XBox360_LeftBumper,
    k_EInputActionOrigin_XBox360_RightBumper,
    k_EInputActionOrigin_XBox360_Start,		//Start
    k_EInputActionOrigin_XBox360_Back,		//Back
    k_EInputActionOrigin_XBox360_LeftTrigger_Pull,
    k_EInputActionOrigin_XBox360_LeftTrigger_Click,
    k_EInputActionOrigin_XBox360_RightTrigger_Pull,
    k_EInputActionOrigin_XBox360_RightTrigger_Click,
    k_EInputActionOrigin_XBox360_LeftStick_Move,
    k_EInputActionOrigin_XBox360_LeftStick_Click,
    k_EInputActionOrigin_XBox360_LeftStick_DPadNorth,
    k_EInputActionOrigin_XBox360_LeftStick_DPadSouth,
    k_EInputActionOrigin_XBox360_LeftStick_DPadWest,
    k_EInputActionOrigin_XBox360_LeftStick_DPadEast,
    k_EInputActionOrigin_XBox360_RightStick_Move,
    k_EInputActionOrigin_XBox360_RightStick_Click,
    k_EInputActionOrigin_XBox360_RightStick_DPadNorth,
    k_EInputActionOrigin_XBox360_RightStick_DPadSouth,
    k_EInputActionOrigin_XBox360_RightStick_DPadWest,
    k_EInputActionOrigin_XBox360_RightStick_DPadEast,
    k_EInputActionOrigin_XBox360_DPad_North,
    k_EInputActionOrigin_XBox360_DPad_South,
    k_EInputActionOrigin_XBox360_DPad_West,
    k_EInputActionOrigin_XBox360_DPad_East,
    k_EInputActionOrigin_XBox360_DPad_Move,
    k_EInputActionOrigin_XBox360_Reserved1,
    k_EInputActionOrigin_XBox360_Reserved2,
    k_EInputActionOrigin_XBox360_Reserved3,
    k_EInputActionOrigin_XBox360_Reserved4,
    k_EInputActionOrigin_XBox360_Reserved5,
    k_EInputActionOrigin_XBox360_Reserved6,
    k_EInputActionOrigin_XBox360_Reserved7,
    k_EInputActionOrigin_XBox360_Reserved8,
    k_EInputActionOrigin_XBox360_Reserved9,
    k_EInputActionOrigin_XBox360_Reserved10,


    // Switch - Pro or Joycons used as a single input device.
    // This does not apply to a single joycon
    k_EInputActionOrigin_Switch_A,
    k_EInputActionOrigin_Switch_B,
    k_EInputActionOrigin_Switch_X,
    k_EInputActionOrigin_Switch_Y,
    k_EInputActionOrigin_Switch_LeftBumper,
    k_EInputActionOrigin_Switch_RightBumper,
    k_EInputActionOrigin_Switch_Plus,	//Start
    k_EInputActionOrigin_Switch_Minus,	//Back
    k_EInputActionOrigin_Switch_Capture,
    k_EInputActionOrigin_Switch_LeftTrigger_Pull,
    k_EInputActionOrigin_Switch_LeftTrigger_Click,
    k_EInputActionOrigin_Switch_RightTrigger_Pull,
    k_EInputActionOrigin_Switch_RightTrigger_Click,
    k_EInputActionOrigin_Switch_LeftStick_Move,
    k_EInputActionOrigin_Switch_LeftStick_Click,
    k_EInputActionOrigin_Switch_LeftStick_DPadNorth,
    k_EInputActionOrigin_Switch_LeftStick_DPadSouth,
    k_EInputActionOrigin_Switch_LeftStick_DPadWest,
    k_EInputActionOrigin_Switch_LeftStick_DPadEast,
    k_EInputActionOrigin_Switch_RightStick_Move,
    k_EInputActionOrigin_Switch_RightStick_Click,
    k_EInputActionOrigin_Switch_RightStick_DPadNorth,
    k_EInputActionOrigin_Switch_RightStick_DPadSouth,
    k_EInputActionOrigin_Switch_RightStick_DPadWest,
    k_EInputActionOrigin_Switch_RightStick_DPadEast,
    k_EInputActionOrigin_Switch_DPad_North,
    k_EInputActionOrigin_Switch_DPad_South,
    k_EInputActionOrigin_Switch_DPad_West,
    k_EInputActionOrigin_Switch_DPad_East,
    k_EInputActionOrigin_Switch_ProGyro_Move,  // Primary Gyro in Pro Controller, or Right JoyCon
    k_EInputActionOrigin_Switch_ProGyro_Pitch,  // Primary Gyro in Pro Controller, or Right JoyCon
    k_EInputActionOrigin_Switch_ProGyro_Yaw,  // Primary Gyro in Pro Controller, or Right JoyCon
    k_EInputActionOrigin_Switch_ProGyro_Roll,  // Primary Gyro in Pro Controller, or Right JoyCon
    k_EInputActionOrigin_Switch_DPad_Move,
    k_EInputActionOrigin_Switch_Reserved1,
    k_EInputActionOrigin_Switch_Reserved2,
    k_EInputActionOrigin_Switch_Reserved3,
    k_EInputActionOrigin_Switch_Reserved4,
    k_EInputActionOrigin_Switch_Reserved5,
    k_EInputActionOrigin_Switch_Reserved6,
    k_EInputActionOrigin_Switch_Reserved7,
    k_EInputActionOrigin_Switch_Reserved8,
    k_EInputActionOrigin_Switch_Reserved9,
    k_EInputActionOrigin_Switch_Reserved10,

    // Switch JoyCon Specific
    k_EInputActionOrigin_Switch_RightGyro_Move,  // Right JoyCon Gyro generally should correspond to Pro's single gyro
    k_EInputActionOrigin_Switch_RightGyro_Pitch,  // Right JoyCon Gyro generally should correspond to Pro's single gyro
    k_EInputActionOrigin_Switch_RightGyro_Yaw,  // Right JoyCon Gyro generally should correspond to Pro's single gyro
    k_EInputActionOrigin_Switch_RightGyro_Roll,  // Right JoyCon Gyro generally should correspond to Pro's single gyro
    k_EInputActionOrigin_Switch_LeftGyro_Move,
    k_EInputActionOrigin_Switch_LeftGyro_Pitch,
    k_EInputActionOrigin_Switch_LeftGyro_Yaw,
    k_EInputActionOrigin_Switch_LeftGyro_Roll,
    k_EInputActionOrigin_Switch_LeftGrip_Lower, // Left JoyCon SR Button
    k_EInputActionOrigin_Switch_LeftGrip_Upper, // Left JoyCon SL Button
    k_EInputActionOrigin_Switch_RightGrip_Lower,  // Right JoyCon SL Button
    k_EInputActionOrigin_Switch_RightGrip_Upper,  // Right JoyCon SR Button
    k_EInputActionOrigin_Switch_JoyConButton_N, // With a Horizontal JoyCon this will be Y or what would be Dpad Right when vertical
    k_EInputActionOrigin_Switch_JoyConButton_E, // X
    k_EInputActionOrigin_Switch_JoyConButton_S, // A
    k_EInputActionOrigin_Switch_JoyConButton_W, // B
    k_EInputActionOrigin_Switch_Reserved15,
    k_EInputActionOrigin_Switch_Reserved16,
    k_EInputActionOrigin_Switch_Reserved17,
    k_EInputActionOrigin_Switch_Reserved18,
    k_EInputActionOrigin_Switch_Reserved19,
    k_EInputActionOrigin_Switch_Reserved20,

    // Added in SDK 1.51
    k_EInputActionOrigin_PS5_X,
    k_EInputActionOrigin_PS5_Circle,
    k_EInputActionOrigin_PS5_Triangle,
    k_EInputActionOrigin_PS5_Square,
    k_EInputActionOrigin_PS5_LeftBumper,
    k_EInputActionOrigin_PS5_RightBumper,
    k_EInputActionOrigin_PS5_Option,	//Start
    k_EInputActionOrigin_PS5_Create,		//Back
    k_EInputActionOrigin_PS5_Mute,
    k_EInputActionOrigin_PS5_LeftPad_Touch,
    k_EInputActionOrigin_PS5_LeftPad_Swipe,
    k_EInputActionOrigin_PS5_LeftPad_Click,
    k_EInputActionOrigin_PS5_LeftPad_DPadNorth,
    k_EInputActionOrigin_PS5_LeftPad_DPadSouth,
    k_EInputActionOrigin_PS5_LeftPad_DPadWest,
    k_EInputActionOrigin_PS5_LeftPad_DPadEast,
    k_EInputActionOrigin_PS5_RightPad_Touch,
    k_EInputActionOrigin_PS5_RightPad_Swipe,
    k_EInputActionOrigin_PS5_RightPad_Click,
    k_EInputActionOrigin_PS5_RightPad_DPadNorth,
    k_EInputActionOrigin_PS5_RightPad_DPadSouth,
    k_EInputActionOrigin_PS5_RightPad_DPadWest,
    k_EInputActionOrigin_PS5_RightPad_DPadEast,
    k_EInputActionOrigin_PS5_CenterPad_Touch,
    k_EInputActionOrigin_PS5_CenterPad_Swipe,
    k_EInputActionOrigin_PS5_CenterPad_Click,
    k_EInputActionOrigin_PS5_CenterPad_DPadNorth,
    k_EInputActionOrigin_PS5_CenterPad_DPadSouth,
    k_EInputActionOrigin_PS5_CenterPad_DPadWest,
    k_EInputActionOrigin_PS5_CenterPad_DPadEast,
    k_EInputActionOrigin_PS5_LeftTrigger_Pull,
    k_EInputActionOrigin_PS5_LeftTrigger_Click,
    k_EInputActionOrigin_PS5_RightTrigger_Pull,
    k_EInputActionOrigin_PS5_RightTrigger_Click,
    k_EInputActionOrigin_PS5_LeftStick_Move,
    k_EInputActionOrigin_PS5_LeftStick_Click,
    k_EInputActionOrigin_PS5_LeftStick_DPadNorth,
    k_EInputActionOrigin_PS5_LeftStick_DPadSouth,
    k_EInputActionOrigin_PS5_LeftStick_DPadWest,
    k_EInputActionOrigin_PS5_LeftStick_DPadEast,
    k_EInputActionOrigin_PS5_RightStick_Move,
    k_EInputActionOrigin_PS5_RightStick_Click,
    k_EInputActionOrigin_PS5_RightStick_DPadNorth,
    k_EInputActionOrigin_PS5_RightStick_DPadSouth,
    k_EInputActionOrigin_PS5_RightStick_DPadWest,
    k_EInputActionOrigin_PS5_RightStick_DPadEast,
    k_EInputActionOrigin_PS5_DPad_North,
    k_EInputActionOrigin_PS5_DPad_South,
    k_EInputActionOrigin_PS5_DPad_West,
    k_EInputActionOrigin_PS5_DPad_East,
    k_EInputActionOrigin_PS5_Gyro_Move,
    k_EInputActionOrigin_PS5_Gyro_Pitch,
    k_EInputActionOrigin_PS5_Gyro_Yaw,
    k_EInputActionOrigin_PS5_Gyro_Roll,
    k_EInputActionOrigin_PS5_DPad_Move,
    k_EInputActionOrigin_PS5_LeftGrip,
    k_EInputActionOrigin_PS5_RightGrip,
    k_EInputActionOrigin_PS5_LeftFn,
    k_EInputActionOrigin_PS5_RightFn,
    k_EInputActionOrigin_PS5_Reserved5,
    k_EInputActionOrigin_PS5_Reserved6,
    k_EInputActionOrigin_PS5_Reserved7,
    k_EInputActionOrigin_PS5_Reserved8,
    k_EInputActionOrigin_PS5_Reserved9,
    k_EInputActionOrigin_PS5_Reserved10,
    k_EInputActionOrigin_PS5_Reserved11,
    k_EInputActionOrigin_PS5_Reserved12,
    k_EInputActionOrigin_PS5_Reserved13,
    k_EInputActionOrigin_PS5_Reserved14,
    k_EInputActionOrigin_PS5_Reserved15,
    k_EInputActionOrigin_PS5_Reserved16,
    k_EInputActionOrigin_PS5_Reserved17,
    k_EInputActionOrigin_PS5_Reserved18,
    k_EInputActionOrigin_PS5_Reserved19,
    k_EInputActionOrigin_PS5_Reserved20,

    // Added in SDK 1.53
    k_EInputActionOrigin_SteamDeck_A,
    k_EInputActionOrigin_SteamDeck_B,
    k_EInputActionOrigin_SteamDeck_X,
    k_EInputActionOrigin_SteamDeck_Y,
    k_EInputActionOrigin_SteamDeck_L1,
    k_EInputActionOrigin_SteamDeck_R1,
    k_EInputActionOrigin_SteamDeck_Menu,
    k_EInputActionOrigin_SteamDeck_View,
    k_EInputActionOrigin_SteamDeck_LeftPad_Touch,
    k_EInputActionOrigin_SteamDeck_LeftPad_Swipe,
    k_EInputActionOrigin_SteamDeck_LeftPad_Click,
    k_EInputActionOrigin_SteamDeck_LeftPad_DPadNorth,
    k_EInputActionOrigin_SteamDeck_LeftPad_DPadSouth,
    k_EInputActionOrigin_SteamDeck_LeftPad_DPadWest,
    k_EInputActionOrigin_SteamDeck_LeftPad_DPadEast,
    k_EInputActionOrigin_SteamDeck_RightPad_Touch,
    k_EInputActionOrigin_SteamDeck_RightPad_Swipe,
    k_EInputActionOrigin_SteamDeck_RightPad_Click,
    k_EInputActionOrigin_SteamDeck_RightPad_DPadNorth,
    k_EInputActionOrigin_SteamDeck_RightPad_DPadSouth,
    k_EInputActionOrigin_SteamDeck_RightPad_DPadWest,
    k_EInputActionOrigin_SteamDeck_RightPad_DPadEast,
    k_EInputActionOrigin_SteamDeck_L2_SoftPull,
    k_EInputActionOrigin_SteamDeck_L2,
    k_EInputActionOrigin_SteamDeck_R2_SoftPull,
    k_EInputActionOrigin_SteamDeck_R2,
    k_EInputActionOrigin_SteamDeck_LeftStick_Move,
    k_EInputActionOrigin_SteamDeck_L3,
    k_EInputActionOrigin_SteamDeck_LeftStick_DPadNorth,
    k_EInputActionOrigin_SteamDeck_LeftStick_DPadSouth,
    k_EInputActionOrigin_SteamDeck_LeftStick_DPadWest,
    k_EInputActionOrigin_SteamDeck_LeftStick_DPadEast,
    k_EInputActionOrigin_SteamDeck_LeftStick_Touch,
    k_EInputActionOrigin_SteamDeck_RightStick_Move,
    k_EInputActionOrigin_SteamDeck_R3,
    k_EInputActionOrigin_SteamDeck_RightStick_DPadNorth,
    k_EInputActionOrigin_SteamDeck_RightStick_DPadSouth,
    k_EInputActionOrigin_SteamDeck_RightStick_DPadWest,
    k_EInputActionOrigin_SteamDeck_RightStick_DPadEast,
    k_EInputActionOrigin_SteamDeck_RightStick_Touch,
    k_EInputActionOrigin_SteamDeck_L4,
    k_EInputActionOrigin_SteamDeck_R4,
    k_EInputActionOrigin_SteamDeck_L5,
    k_EInputActionOrigin_SteamDeck_R5,
    k_EInputActionOrigin_SteamDeck_DPad_Move,
    k_EInputActionOrigin_SteamDeck_DPad_North,
    k_EInputActionOrigin_SteamDeck_DPad_South,
    k_EInputActionOrigin_SteamDeck_DPad_West,
    k_EInputActionOrigin_SteamDeck_DPad_East,
    k_EInputActionOrigin_SteamDeck_Gyro_Move,
    k_EInputActionOrigin_SteamDeck_Gyro_Pitch,
    k_EInputActionOrigin_SteamDeck_Gyro_Yaw,
    k_EInputActionOrigin_SteamDeck_Gyro_Roll,
    k_EInputActionOrigin_SteamDeck_Reserved1,
    k_EInputActionOrigin_SteamDeck_Reserved2,
    k_EInputActionOrigin_SteamDeck_Reserved3,
    k_EInputActionOrigin_SteamDeck_Reserved4,
    k_EInputActionOrigin_SteamDeck_Reserved5,
    k_EInputActionOrigin_SteamDeck_Reserved6,
    k_EInputActionOrigin_SteamDeck_Reserved7,
    k_EInputActionOrigin_SteamDeck_Reserved8,
    k_EInputActionOrigin_SteamDeck_Reserved9,
    k_EInputActionOrigin_SteamDeck_Reserved10,
    k_EInputActionOrigin_SteamDeck_Reserved11,
    k_EInputActionOrigin_SteamDeck_Reserved12,
    k_EInputActionOrigin_SteamDeck_Reserved13,
    k_EInputActionOrigin_SteamDeck_Reserved14,
    k_EInputActionOrigin_SteamDeck_Reserved15,
    k_EInputActionOrigin_SteamDeck_Reserved16,
    k_EInputActionOrigin_SteamDeck_Reserved17,
    k_EInputActionOrigin_SteamDeck_Reserved18,
    k_EInputActionOrigin_SteamDeck_Reserved19,
    k_EInputActionOrigin_SteamDeck_Reserved20,

    k_EInputActionOrigin_Count, // If Steam has added support for new controllers origins will go here.
    k_EInputActionOrigin_MaximumPossibleValue = 32767, // Origins are currently a maximum of 16 bits.
} EInputActionOrigin;

typedef enum EXboxOrigin
{
    k_EXboxOrigin_A,
    k_EXboxOrigin_B,
    k_EXboxOrigin_X,
    k_EXboxOrigin_Y,
    k_EXboxOrigin_LeftBumper,
    k_EXboxOrigin_RightBumper,
    k_EXboxOrigin_Menu,  //Start
    k_EXboxOrigin_View,  //Back
    k_EXboxOrigin_LeftTrigger_Pull,
    k_EXboxOrigin_LeftTrigger_Click,
    k_EXboxOrigin_RightTrigger_Pull,
    k_EXboxOrigin_RightTrigger_Click,
    k_EXboxOrigin_LeftStick_Move,
    k_EXboxOrigin_LeftStick_Click,
    k_EXboxOrigin_LeftStick_DPadNorth,
    k_EXboxOrigin_LeftStick_DPadSouth,
    k_EXboxOrigin_LeftStick_DPadWest,
    k_EXboxOrigin_LeftStick_DPadEast,
    k_EXboxOrigin_RightStick_Move,
    k_EXboxOrigin_RightStick_Click,
    k_EXboxOrigin_RightStick_DPadNorth,
    k_EXboxOrigin_RightStick_DPadSouth,
    k_EXboxOrigin_RightStick_DPadWest,
    k_EXboxOrigin_RightStick_DPadEast,
    k_EXboxOrigin_DPad_North,
    k_EXboxOrigin_DPad_South,
    k_EXboxOrigin_DPad_West,
    k_EXboxOrigin_DPad_East,
    k_EXboxOrigin_Count,
} EXboxOrigin;

typedef enum ESteamControllerPad
{
    k_ESteamControllerPad_Left,
    k_ESteamControllerPad_Right
} ESteamControllerPad;

typedef enum EControllerHapticLocation
{
    k_EControllerHapticLocation_Left = ( 1 << k_ESteamControllerPad_Left ),
    k_EControllerHapticLocation_Right = ( 1 << k_ESteamControllerPad_Right ),
    k_EControllerHapticLocation_Both = ( 1 << k_ESteamControllerPad_Left | 1 << k_ESteamControllerPad_Right ),
} EControllerHapticLocation;

typedef enum EControllerHapticType
{
    k_EControllerHapticType_Off,
    k_EControllerHapticType_Tick,
    k_EControllerHapticType_Click,
} EControllerHapticType;

typedef enum ESteamInputType
{
    k_ESteamInputType_Unknown,
    k_ESteamInputType_SteamController,
    k_ESteamInputType_XBox360Controller,
    k_ESteamInputType_XBoxOneController,
    k_ESteamInputType_GenericGamepad,		// DirectInput controllers
    k_ESteamInputType_PS4Controller,
    k_ESteamInputType_AppleMFiController,	// Unused
    k_ESteamInputType_AndroidController,	// Unused
    k_ESteamInputType_SwitchJoyConPair,		// Unused
    k_ESteamInputType_SwitchJoyConSingle,	// Unused
    k_ESteamInputType_SwitchProController,
    k_ESteamInputType_MobileTouch,			// Steam Link App On-screen Virtual Controller
    k_ESteamInputType_PS3Controller,		// Currently uses PS4 Origins
    k_ESteamInputType_PS5Controller,		// Added in SDK 151
    k_ESteamInputType_SteamDeckController,	// Added in SDK 153
    k_ESteamInputType_Count,
    k_ESteamInputType_MaximumPossibleValue = 255,
} ESteamInputType;

// Individual values are used by the GetSessionInputConfigurationSettings bitmask
typedef enum ESteamInputConfigurationEnableType
{
    k_ESteamInputConfigurationEnableType_None			= 0x0000,
    k_ESteamInputConfigurationEnableType_Playstation	= 0x0001,
    k_ESteamInputConfigurationEnableType_Xbox			= 0x0002,
    k_ESteamInputConfigurationEnableType_Generic		= 0x0004,
    k_ESteamInputConfigurationEnableType_Switch			= 0x0008,
} ESteamInputConfigurationEnableType;

// These values are passed into SetLEDColor
typedef enum ESteamInputLEDFlag
{
    k_ESteamInputLEDFlag_SetColor,
    // Restore the LED color to the user's preference setting as set in the controller personalization menu.
    // This also happens automatically on exit of your game.
    k_ESteamInputLEDFlag_RestoreUserDefault
} ESteamInputLEDFlag;

// These values are passed into GetGlyphPNGForActionOrigin
typedef enum ESteamInputGlyphSize
{
    k_ESteamInputGlyphSize_Small,	// 32x32 pixels
    k_ESteamInputGlyphSize_Medium,	// 128x128 pixels
    k_ESteamInputGlyphSize_Large,	// 256x256 pixels
    k_ESteamInputGlyphSize_Count,
} ESteamInputGlyphSize;

typedef enum ESteamInputGlyphStyle
{
    // Base-styles - cannot mix
    ESteamInputGlyphStyle_Knockout 	= 0x0, // Face buttons will have colored labels/outlines on a knocked out background
    // Rest of inputs will have white detail/borders on a knocked out background
    ESteamInputGlyphStyle_Light		= 0x1, // Black detail/borders on a white background
    ESteamInputGlyphStyle_Dark 		= 0x2, // White detail/borders on a black background

    // Modifiers
    // Default ABXY/PS equivalent glyphs have a solid fill w/ color matching the physical buttons on the device
    ESteamInputGlyphStyle_NeutralColorABXY 	= 0x10, // ABXY Buttons will match the base style color instead of their normal associated color
    ESteamInputGlyphStyle_SolidABXY 		= 0x20,	// ABXY Buttons will have a solid fill
} ESteamInputGlyphStyle;

typedef enum ESteamInputActionEventType
{
    ESteamInputActionEventType_DigitalAction,
    ESteamInputActionEventType_AnalogAction,
} ESteamInputActionEventType;

// InputHandle_t is used to refer to a specific controller.
// This handle will consistently identify a controller, even if it is disconnected and re-connected
typedef uint64 InputHandle_t;

// These handles are used to refer to a specific in-game action or action set
// All action handles should be queried during initialization for performance reasons
typedef uint64 InputActionSetHandle_t;
typedef uint64 InputDigitalActionHandle_t;
typedef uint64 InputAnalogActionHandle_t;

#pragma pack( push, 1 )

typedef struct InputAnalogActionData_t
{
    // Type of data coming from this action, this will match what got specified in the action set
    EInputSourceMode eMode;

    // The current state of this action; will be delta updates for mouse actions
    float x, y;

    // Whether or not this action is currently available to be bound in the active action set
    bool bActive;
} InputAnalogActionData_t;

typedef struct InputDigitalActionData_t
{
    // The current state of this action; will be true if currently pressed
    bool bState;

    // Whether or not this action is currently available to be bound in the active action set
    bool bActive;
} InputDigitalActionData_t;

typedef struct InputMotionData_t
{
    // Sensor-fused absolute rotation; will drift in heading toward average
    float rotQuatX;
    float rotQuatY;
    float rotQuatZ;
    float rotQuatW;

    // Positional acceleration
    float posAccelX;
    float posAccelY;
    float posAccelZ;

    // Angular velocity
    float rotVelX;
    float rotVelY;
    float rotVelZ;
} InputMotionData_t;

//-----------------------------------------------------------------------------
// Purpose: when callbacks are enabled this fires each time a controller action
// state changes
//-----------------------------------------------------------------------------
typedef struct SteamInputActionEvent_t
{
    InputHandle_t controllerHandle;
    ESteamInputActionEventType eEventType;
    struct AnalogAction_t {
        InputAnalogActionHandle_t actionHandle;
        InputAnalogActionData_t analogActionData;
    };
    struct DigitalAction_t {
        InputDigitalActionHandle_t actionHandle;
        InputDigitalActionData_t digitalActionData;
    };
    union {
        AnalogAction_t analogAction;
        DigitalAction_t digitalAction;
    };
} SteamInputActionEvent_t;

//-----------------------------------------------------------------------------
// Forward declaration for ScePadTriggerEffectParam, defined in isteamdualsense.h
//-----------------------------------------------------------------------------
typedef struct ScePadTriggerEffectParam ScePadTriggerEffectParam;

#pragma pack( pop )

typedef void ( *SteamInputActionEventCallbackPointer )( SteamInputActionEvent_t * );

// Note: Please do not use action origins as a way to identify controller types. There is no
// guarantee that they will be added in a contiguous manner - use GetInputTypeForHandle instead
// Versions of Steam that add new controller types in the future will extend this enum if you're
// using a lookup table please check the bounds of any origins returned by Steam.
typedef enum EControllerActionOrigin
{
    // Steam Controller
    k_EControllerActionOrigin_None,
    k_EControllerActionOrigin_A,
    k_EControllerActionOrigin_B,
    k_EControllerActionOrigin_X,
    k_EControllerActionOrigin_Y,
    k_EControllerActionOrigin_LeftBumper,
    k_EControllerActionOrigin_RightBumper,
    k_EControllerActionOrigin_LeftGrip,
    k_EControllerActionOrigin_RightGrip,
    k_EControllerActionOrigin_Start,
    k_EControllerActionOrigin_Back,
    k_EControllerActionOrigin_LeftPad_Touch,
    k_EControllerActionOrigin_LeftPad_Swipe,
    k_EControllerActionOrigin_LeftPad_Click,
    k_EControllerActionOrigin_LeftPad_DPadNorth,
    k_EControllerActionOrigin_LeftPad_DPadSouth,
    k_EControllerActionOrigin_LeftPad_DPadWest,
    k_EControllerActionOrigin_LeftPad_DPadEast,
    k_EControllerActionOrigin_RightPad_Touch,
    k_EControllerActionOrigin_RightPad_Swipe,
    k_EControllerActionOrigin_RightPad_Click,
    k_EControllerActionOrigin_RightPad_DPadNorth,
    k_EControllerActionOrigin_RightPad_DPadSouth,
    k_EControllerActionOrigin_RightPad_DPadWest,
    k_EControllerActionOrigin_RightPad_DPadEast,
    k_EControllerActionOrigin_LeftTrigger_Pull,
    k_EControllerActionOrigin_LeftTrigger_Click,
    k_EControllerActionOrigin_RightTrigger_Pull,
    k_EControllerActionOrigin_RightTrigger_Click,
    k_EControllerActionOrigin_LeftStick_Move,
    k_EControllerActionOrigin_LeftStick_Click,
    k_EControllerActionOrigin_LeftStick_DPadNorth,
    k_EControllerActionOrigin_LeftStick_DPadSouth,
    k_EControllerActionOrigin_LeftStick_DPadWest,
    k_EControllerActionOrigin_LeftStick_DPadEast,
    k_EControllerActionOrigin_Gyro_Move,
    k_EControllerActionOrigin_Gyro_Pitch,
    k_EControllerActionOrigin_Gyro_Yaw,
    k_EControllerActionOrigin_Gyro_Roll,

    // PS4 Dual Shock
    k_EControllerActionOrigin_PS4_X,
    k_EControllerActionOrigin_PS4_Circle,
    k_EControllerActionOrigin_PS4_Triangle,
    k_EControllerActionOrigin_PS4_Square,
    k_EControllerActionOrigin_PS4_LeftBumper,
    k_EControllerActionOrigin_PS4_RightBumper,
    k_EControllerActionOrigin_PS4_Options,  //Start
    k_EControllerActionOrigin_PS4_Share,	//Back
    k_EControllerActionOrigin_PS4_LeftPad_Touch,
    k_EControllerActionOrigin_PS4_LeftPad_Swipe,
    k_EControllerActionOrigin_PS4_LeftPad_Click,
    k_EControllerActionOrigin_PS4_LeftPad_DPadNorth,
    k_EControllerActionOrigin_PS4_LeftPad_DPadSouth,
    k_EControllerActionOrigin_PS4_LeftPad_DPadWest,
    k_EControllerActionOrigin_PS4_LeftPad_DPadEast,
    k_EControllerActionOrigin_PS4_RightPad_Touch,
    k_EControllerActionOrigin_PS4_RightPad_Swipe,
    k_EControllerActionOrigin_PS4_RightPad_Click,
    k_EControllerActionOrigin_PS4_RightPad_DPadNorth,
    k_EControllerActionOrigin_PS4_RightPad_DPadSouth,
    k_EControllerActionOrigin_PS4_RightPad_DPadWest,
    k_EControllerActionOrigin_PS4_RightPad_DPadEast,
    k_EControllerActionOrigin_PS4_CenterPad_Touch,
    k_EControllerActionOrigin_PS4_CenterPad_Swipe,
    k_EControllerActionOrigin_PS4_CenterPad_Click,
    k_EControllerActionOrigin_PS4_CenterPad_DPadNorth,
    k_EControllerActionOrigin_PS4_CenterPad_DPadSouth,
    k_EControllerActionOrigin_PS4_CenterPad_DPadWest,
    k_EControllerActionOrigin_PS4_CenterPad_DPadEast,
    k_EControllerActionOrigin_PS4_LeftTrigger_Pull,
    k_EControllerActionOrigin_PS4_LeftTrigger_Click,
    k_EControllerActionOrigin_PS4_RightTrigger_Pull,
    k_EControllerActionOrigin_PS4_RightTrigger_Click,
    k_EControllerActionOrigin_PS4_LeftStick_Move,
    k_EControllerActionOrigin_PS4_LeftStick_Click,
    k_EControllerActionOrigin_PS4_LeftStick_DPadNorth,
    k_EControllerActionOrigin_PS4_LeftStick_DPadSouth,
    k_EControllerActionOrigin_PS4_LeftStick_DPadWest,
    k_EControllerActionOrigin_PS4_LeftStick_DPadEast,
    k_EControllerActionOrigin_PS4_RightStick_Move,
    k_EControllerActionOrigin_PS4_RightStick_Click,
    k_EControllerActionOrigin_PS4_RightStick_DPadNorth,
    k_EControllerActionOrigin_PS4_RightStick_DPadSouth,
    k_EControllerActionOrigin_PS4_RightStick_DPadWest,
    k_EControllerActionOrigin_PS4_RightStick_DPadEast,
    k_EControllerActionOrigin_PS4_DPad_North,
    k_EControllerActionOrigin_PS4_DPad_South,
    k_EControllerActionOrigin_PS4_DPad_West,
    k_EControllerActionOrigin_PS4_DPad_East,
    k_EControllerActionOrigin_PS4_Gyro_Move,
    k_EControllerActionOrigin_PS4_Gyro_Pitch,
    k_EControllerActionOrigin_PS4_Gyro_Yaw,
    k_EControllerActionOrigin_PS4_Gyro_Roll,

    // XBox One
    k_EControllerActionOrigin_XBoxOne_A,
    k_EControllerActionOrigin_XBoxOne_B,
    k_EControllerActionOrigin_XBoxOne_X,
    k_EControllerActionOrigin_XBoxOne_Y,
    k_EControllerActionOrigin_XBoxOne_LeftBumper,
    k_EControllerActionOrigin_XBoxOne_RightBumper,
    k_EControllerActionOrigin_XBoxOne_Menu,  //Start
    k_EControllerActionOrigin_XBoxOne_View,  //Back
    k_EControllerActionOrigin_XBoxOne_LeftTrigger_Pull,
    k_EControllerActionOrigin_XBoxOne_LeftTrigger_Click,
    k_EControllerActionOrigin_XBoxOne_RightTrigger_Pull,
    k_EControllerActionOrigin_XBoxOne_RightTrigger_Click,
    k_EControllerActionOrigin_XBoxOne_LeftStick_Move,
    k_EControllerActionOrigin_XBoxOne_LeftStick_Click,
    k_EControllerActionOrigin_XBoxOne_LeftStick_DPadNorth,
    k_EControllerActionOrigin_XBoxOne_LeftStick_DPadSouth,
    k_EControllerActionOrigin_XBoxOne_LeftStick_DPadWest,
    k_EControllerActionOrigin_XBoxOne_LeftStick_DPadEast,
    k_EControllerActionOrigin_XBoxOne_RightStick_Move,
    k_EControllerActionOrigin_XBoxOne_RightStick_Click,
    k_EControllerActionOrigin_XBoxOne_RightStick_DPadNorth,
    k_EControllerActionOrigin_XBoxOne_RightStick_DPadSouth,
    k_EControllerActionOrigin_XBoxOne_RightStick_DPadWest,
    k_EControllerActionOrigin_XBoxOne_RightStick_DPadEast,
    k_EControllerActionOrigin_XBoxOne_DPad_North,
    k_EControllerActionOrigin_XBoxOne_DPad_South,
    k_EControllerActionOrigin_XBoxOne_DPad_West,
    k_EControllerActionOrigin_XBoxOne_DPad_East,

    // XBox 360
    k_EControllerActionOrigin_XBox360_A,
    k_EControllerActionOrigin_XBox360_B,
    k_EControllerActionOrigin_XBox360_X,
    k_EControllerActionOrigin_XBox360_Y,
    k_EControllerActionOrigin_XBox360_LeftBumper,
    k_EControllerActionOrigin_XBox360_RightBumper,
    k_EControllerActionOrigin_XBox360_Start,  //Start
    k_EControllerActionOrigin_XBox360_Back,  //Back
    k_EControllerActionOrigin_XBox360_LeftTrigger_Pull,
    k_EControllerActionOrigin_XBox360_LeftTrigger_Click,
    k_EControllerActionOrigin_XBox360_RightTrigger_Pull,
    k_EControllerActionOrigin_XBox360_RightTrigger_Click,
    k_EControllerActionOrigin_XBox360_LeftStick_Move,
    k_EControllerActionOrigin_XBox360_LeftStick_Click,
    k_EControllerActionOrigin_XBox360_LeftStick_DPadNorth,
    k_EControllerActionOrigin_XBox360_LeftStick_DPadSouth,
    k_EControllerActionOrigin_XBox360_LeftStick_DPadWest,
    k_EControllerActionOrigin_XBox360_LeftStick_DPadEast,
    k_EControllerActionOrigin_XBox360_RightStick_Move,
    k_EControllerActionOrigin_XBox360_RightStick_Click,
    k_EControllerActionOrigin_XBox360_RightStick_DPadNorth,
    k_EControllerActionOrigin_XBox360_RightStick_DPadSouth,
    k_EControllerActionOrigin_XBox360_RightStick_DPadWest,
    k_EControllerActionOrigin_XBox360_RightStick_DPadEast,
    k_EControllerActionOrigin_XBox360_DPad_North,
    k_EControllerActionOrigin_XBox360_DPad_South,
    k_EControllerActionOrigin_XBox360_DPad_West,
    k_EControllerActionOrigin_XBox360_DPad_East,

    // SteamController V2
    k_EControllerActionOrigin_SteamV2_A,
    k_EControllerActionOrigin_SteamV2_B,
    k_EControllerActionOrigin_SteamV2_X,
    k_EControllerActionOrigin_SteamV2_Y,
    k_EControllerActionOrigin_SteamV2_LeftBumper,
    k_EControllerActionOrigin_SteamV2_RightBumper,
    k_EControllerActionOrigin_SteamV2_LeftGrip_Lower,
    k_EControllerActionOrigin_SteamV2_LeftGrip_Upper,
    k_EControllerActionOrigin_SteamV2_RightGrip_Lower,
    k_EControllerActionOrigin_SteamV2_RightGrip_Upper,
    k_EControllerActionOrigin_SteamV2_LeftBumper_Pressure,
    k_EControllerActionOrigin_SteamV2_RightBumper_Pressure,
    k_EControllerActionOrigin_SteamV2_LeftGrip_Pressure,
    k_EControllerActionOrigin_SteamV2_RightGrip_Pressure,
    k_EControllerActionOrigin_SteamV2_LeftGrip_Upper_Pressure,
    k_EControllerActionOrigin_SteamV2_RightGrip_Upper_Pressure,
    k_EControllerActionOrigin_SteamV2_Start,
    k_EControllerActionOrigin_SteamV2_Back,
    k_EControllerActionOrigin_SteamV2_LeftPad_Touch,
    k_EControllerActionOrigin_SteamV2_LeftPad_Swipe,
    k_EControllerActionOrigin_SteamV2_LeftPad_Click,
    k_EControllerActionOrigin_SteamV2_LeftPad_Pressure,
    k_EControllerActionOrigin_SteamV2_LeftPad_DPadNorth,
    k_EControllerActionOrigin_SteamV2_LeftPad_DPadSouth,
    k_EControllerActionOrigin_SteamV2_LeftPad_DPadWest,
    k_EControllerActionOrigin_SteamV2_LeftPad_DPadEast,
    k_EControllerActionOrigin_SteamV2_RightPad_Touch,
    k_EControllerActionOrigin_SteamV2_RightPad_Swipe,
    k_EControllerActionOrigin_SteamV2_RightPad_Click,
    k_EControllerActionOrigin_SteamV2_RightPad_Pressure,
    k_EControllerActionOrigin_SteamV2_RightPad_DPadNorth,
    k_EControllerActionOrigin_SteamV2_RightPad_DPadSouth,
    k_EControllerActionOrigin_SteamV2_RightPad_DPadWest,
    k_EControllerActionOrigin_SteamV2_RightPad_DPadEast,
    k_EControllerActionOrigin_SteamV2_LeftTrigger_Pull,
    k_EControllerActionOrigin_SteamV2_LeftTrigger_Click,
    k_EControllerActionOrigin_SteamV2_RightTrigger_Pull,
    k_EControllerActionOrigin_SteamV2_RightTrigger_Click,
    k_EControllerActionOrigin_SteamV2_LeftStick_Move,
    k_EControllerActionOrigin_SteamV2_LeftStick_Click,
    k_EControllerActionOrigin_SteamV2_LeftStick_DPadNorth,
    k_EControllerActionOrigin_SteamV2_LeftStick_DPadSouth,
    k_EControllerActionOrigin_SteamV2_LeftStick_DPadWest,
    k_EControllerActionOrigin_SteamV2_LeftStick_DPadEast,
    k_EControllerActionOrigin_SteamV2_Gyro_Move,
    k_EControllerActionOrigin_SteamV2_Gyro_Pitch,
    k_EControllerActionOrigin_SteamV2_Gyro_Yaw,
    k_EControllerActionOrigin_SteamV2_Gyro_Roll,

    // Switch - Pro or Joycons used as a single input device.
    // This does not apply to a single joycon
    k_EControllerActionOrigin_Switch_A,
    k_EControllerActionOrigin_Switch_B,
    k_EControllerActionOrigin_Switch_X,
    k_EControllerActionOrigin_Switch_Y,
    k_EControllerActionOrigin_Switch_LeftBumper,
    k_EControllerActionOrigin_Switch_RightBumper,
    k_EControllerActionOrigin_Switch_Plus,  //Start
    k_EControllerActionOrigin_Switch_Minus,	//Back
    k_EControllerActionOrigin_Switch_Capture,
    k_EControllerActionOrigin_Switch_LeftTrigger_Pull,
    k_EControllerActionOrigin_Switch_LeftTrigger_Click,
    k_EControllerActionOrigin_Switch_RightTrigger_Pull,
    k_EControllerActionOrigin_Switch_RightTrigger_Click,
    k_EControllerActionOrigin_Switch_LeftStick_Move,
    k_EControllerActionOrigin_Switch_LeftStick_Click,
    k_EControllerActionOrigin_Switch_LeftStick_DPadNorth,
    k_EControllerActionOrigin_Switch_LeftStick_DPadSouth,
    k_EControllerActionOrigin_Switch_LeftStick_DPadWest,
    k_EControllerActionOrigin_Switch_LeftStick_DPadEast,
    k_EControllerActionOrigin_Switch_RightStick_Move,
    k_EControllerActionOrigin_Switch_RightStick_Click,
    k_EControllerActionOrigin_Switch_RightStick_DPadNorth,
    k_EControllerActionOrigin_Switch_RightStick_DPadSouth,
    k_EControllerActionOrigin_Switch_RightStick_DPadWest,
    k_EControllerActionOrigin_Switch_RightStick_DPadEast,
    k_EControllerActionOrigin_Switch_DPad_North,
    k_EControllerActionOrigin_Switch_DPad_South,
    k_EControllerActionOrigin_Switch_DPad_West,
    k_EControllerActionOrigin_Switch_DPad_East,
    k_EControllerActionOrigin_Switch_ProGyro_Move,  // Primary Gyro in Pro Controller, or Right JoyCon
    k_EControllerActionOrigin_Switch_ProGyro_Pitch,  // Primary Gyro in Pro Controller, or Right JoyCon
    k_EControllerActionOrigin_Switch_ProGyro_Yaw,  // Primary Gyro in Pro Controller, or Right JoyCon
    k_EControllerActionOrigin_Switch_ProGyro_Roll,  // Primary Gyro in Pro Controller, or Right JoyCon
    // Switch JoyCon Specific
    k_EControllerActionOrigin_Switch_RightGyro_Move,  // Right JoyCon Gyro generally should correspond to Pro's single gyro
    k_EControllerActionOrigin_Switch_RightGyro_Pitch,  // Right JoyCon Gyro generally should correspond to Pro's single gyro
    k_EControllerActionOrigin_Switch_RightGyro_Yaw,  // Right JoyCon Gyro generally should correspond to Pro's single gyro
    k_EControllerActionOrigin_Switch_RightGyro_Roll,  // Right JoyCon Gyro generally should correspond to Pro's single gyro
    k_EControllerActionOrigin_Switch_LeftGyro_Move,
    k_EControllerActionOrigin_Switch_LeftGyro_Pitch,
    k_EControllerActionOrigin_Switch_LeftGyro_Yaw,
    k_EControllerActionOrigin_Switch_LeftGyro_Roll,
    k_EControllerActionOrigin_Switch_LeftGrip_Lower, // Left JoyCon SR Button
    k_EControllerActionOrigin_Switch_LeftGrip_Upper, // Left JoyCon SL Button
    k_EControllerActionOrigin_Switch_RightGrip_Lower,  // Right JoyCon SL Button
    k_EControllerActionOrigin_Switch_RightGrip_Upper,  // Right JoyCon SR Button

    // Added in SDK 1.45
    k_EControllerActionOrigin_PS4_DPad_Move,
    k_EControllerActionOrigin_XBoxOne_DPad_Move,
    k_EControllerActionOrigin_XBox360_DPad_Move,
    k_EControllerActionOrigin_Switch_DPad_Move,

    // Added in SDK 1.51
    k_EControllerActionOrigin_PS5_X,
    k_EControllerActionOrigin_PS5_Circle,
    k_EControllerActionOrigin_PS5_Triangle,
    k_EControllerActionOrigin_PS5_Square,
    k_EControllerActionOrigin_PS5_LeftBumper,
    k_EControllerActionOrigin_PS5_RightBumper,
    k_EControllerActionOrigin_PS5_Option,  //Start
    k_EControllerActionOrigin_PS5_Create,	//Back
    k_EControllerActionOrigin_PS5_Mute,
    k_EControllerActionOrigin_PS5_LeftPad_Touch,
    k_EControllerActionOrigin_PS5_LeftPad_Swipe,
    k_EControllerActionOrigin_PS5_LeftPad_Click,
    k_EControllerActionOrigin_PS5_LeftPad_DPadNorth,
    k_EControllerActionOrigin_PS5_LeftPad_DPadSouth,
    k_EControllerActionOrigin_PS5_LeftPad_DPadWest,
    k_EControllerActionOrigin_PS5_LeftPad_DPadEast,
    k_EControllerActionOrigin_PS5_RightPad_Touch,
    k_EControllerActionOrigin_PS5_RightPad_Swipe,
    k_EControllerActionOrigin_PS5_RightPad_Click,
    k_EControllerActionOrigin_PS5_RightPad_DPadNorth,
    k_EControllerActionOrigin_PS5_RightPad_DPadSouth,
    k_EControllerActionOrigin_PS5_RightPad_DPadWest,
    k_EControllerActionOrigin_PS5_RightPad_DPadEast,
    k_EControllerActionOrigin_PS5_CenterPad_Touch,
    k_EControllerActionOrigin_PS5_CenterPad_Swipe,
    k_EControllerActionOrigin_PS5_CenterPad_Click,
    k_EControllerActionOrigin_PS5_CenterPad_DPadNorth,
    k_EControllerActionOrigin_PS5_CenterPad_DPadSouth,
    k_EControllerActionOrigin_PS5_CenterPad_DPadWest,
    k_EControllerActionOrigin_PS5_CenterPad_DPadEast,
    k_EControllerActionOrigin_PS5_LeftTrigger_Pull,
    k_EControllerActionOrigin_PS5_LeftTrigger_Click,
    k_EControllerActionOrigin_PS5_RightTrigger_Pull,
    k_EControllerActionOrigin_PS5_RightTrigger_Click,
    k_EControllerActionOrigin_PS5_LeftStick_Move,
    k_EControllerActionOrigin_PS5_LeftStick_Click,
    k_EControllerActionOrigin_PS5_LeftStick_DPadNorth,
    k_EControllerActionOrigin_PS5_LeftStick_DPadSouth,
    k_EControllerActionOrigin_PS5_LeftStick_DPadWest,
    k_EControllerActionOrigin_PS5_LeftStick_DPadEast,
    k_EControllerActionOrigin_PS5_RightStick_Move,
    k_EControllerActionOrigin_PS5_RightStick_Click,
    k_EControllerActionOrigin_PS5_RightStick_DPadNorth,
    k_EControllerActionOrigin_PS5_RightStick_DPadSouth,
    k_EControllerActionOrigin_PS5_RightStick_DPadWest,
    k_EControllerActionOrigin_PS5_RightStick_DPadEast,
    k_EControllerActionOrigin_PS5_DPad_Move,
    k_EControllerActionOrigin_PS5_DPad_North,
    k_EControllerActionOrigin_PS5_DPad_South,
    k_EControllerActionOrigin_PS5_DPad_West,
    k_EControllerActionOrigin_PS5_DPad_East,
    k_EControllerActionOrigin_PS5_Gyro_Move,
    k_EControllerActionOrigin_PS5_Gyro_Pitch,
    k_EControllerActionOrigin_PS5_Gyro_Yaw,
    k_EControllerActionOrigin_PS5_Gyro_Roll,

    k_EControllerActionOrigin_XBoxOne_LeftGrip_Lower,
    k_EControllerActionOrigin_XBoxOne_LeftGrip_Upper,
    k_EControllerActionOrigin_XBoxOne_RightGrip_Lower,
    k_EControllerActionOrigin_XBoxOne_RightGrip_Upper,
    k_EControllerActionOrigin_XBoxOne_Share,

    // Added in SDK 1.53
    k_EControllerActionOrigin_SteamDeck_A,
    k_EControllerActionOrigin_SteamDeck_B,
    k_EControllerActionOrigin_SteamDeck_X,
    k_EControllerActionOrigin_SteamDeck_Y,
    k_EControllerActionOrigin_SteamDeck_L1,
    k_EControllerActionOrigin_SteamDeck_R1,
    k_EControllerActionOrigin_SteamDeck_Menu,
    k_EControllerActionOrigin_SteamDeck_View,
    k_EControllerActionOrigin_SteamDeck_LeftPad_Touch,
    k_EControllerActionOrigin_SteamDeck_LeftPad_Swipe,
    k_EControllerActionOrigin_SteamDeck_LeftPad_Click,
    k_EControllerActionOrigin_SteamDeck_LeftPad_DPadNorth,
    k_EControllerActionOrigin_SteamDeck_LeftPad_DPadSouth,
    k_EControllerActionOrigin_SteamDeck_LeftPad_DPadWest,
    k_EControllerActionOrigin_SteamDeck_LeftPad_DPadEast,
    k_EControllerActionOrigin_SteamDeck_RightPad_Touch,
    k_EControllerActionOrigin_SteamDeck_RightPad_Swipe,
    k_EControllerActionOrigin_SteamDeck_RightPad_Click,
    k_EControllerActionOrigin_SteamDeck_RightPad_DPadNorth,
    k_EControllerActionOrigin_SteamDeck_RightPad_DPadSouth,
    k_EControllerActionOrigin_SteamDeck_RightPad_DPadWest,
    k_EControllerActionOrigin_SteamDeck_RightPad_DPadEast,
    k_EControllerActionOrigin_SteamDeck_L2_SoftPull,
    k_EControllerActionOrigin_SteamDeck_L2,
    k_EControllerActionOrigin_SteamDeck_R2_SoftPull,
    k_EControllerActionOrigin_SteamDeck_R2,
    k_EControllerActionOrigin_SteamDeck_LeftStick_Move,
    k_EControllerActionOrigin_SteamDeck_L3,
    k_EControllerActionOrigin_SteamDeck_LeftStick_DPadNorth,
    k_EControllerActionOrigin_SteamDeck_LeftStick_DPadSouth,
    k_EControllerActionOrigin_SteamDeck_LeftStick_DPadWest,
    k_EControllerActionOrigin_SteamDeck_LeftStick_DPadEast,
    k_EControllerActionOrigin_SteamDeck_LeftStick_Touch,
    k_EControllerActionOrigin_SteamDeck_RightStick_Move,
    k_EControllerActionOrigin_SteamDeck_R3,
    k_EControllerActionOrigin_SteamDeck_RightStick_DPadNorth,
    k_EControllerActionOrigin_SteamDeck_RightStick_DPadSouth,
    k_EControllerActionOrigin_SteamDeck_RightStick_DPadWest,
    k_EControllerActionOrigin_SteamDeck_RightStick_DPadEast,
    k_EControllerActionOrigin_SteamDeck_RightStick_Touch,
    k_EControllerActionOrigin_SteamDeck_L4,
    k_EControllerActionOrigin_SteamDeck_R4,
    k_EControllerActionOrigin_SteamDeck_L5,
    k_EControllerActionOrigin_SteamDeck_R5,
    k_EControllerActionOrigin_SteamDeck_DPad_Move,
    k_EControllerActionOrigin_SteamDeck_DPad_North,
    k_EControllerActionOrigin_SteamDeck_DPad_South,
    k_EControllerActionOrigin_SteamDeck_DPad_West,
    k_EControllerActionOrigin_SteamDeck_DPad_East,
    k_EControllerActionOrigin_SteamDeck_Gyro_Move,
    k_EControllerActionOrigin_SteamDeck_Gyro_Pitch,
    k_EControllerActionOrigin_SteamDeck_Gyro_Yaw,
    k_EControllerActionOrigin_SteamDeck_Gyro_Roll,
    k_EControllerActionOrigin_SteamDeck_Reserved1,
    k_EControllerActionOrigin_SteamDeck_Reserved2,
    k_EControllerActionOrigin_SteamDeck_Reserved3,
    k_EControllerActionOrigin_SteamDeck_Reserved4,
    k_EControllerActionOrigin_SteamDeck_Reserved5,
    k_EControllerActionOrigin_SteamDeck_Reserved6,
    k_EControllerActionOrigin_SteamDeck_Reserved7,
    k_EControllerActionOrigin_SteamDeck_Reserved8,
    k_EControllerActionOrigin_SteamDeck_Reserved9,
    k_EControllerActionOrigin_SteamDeck_Reserved10,
    k_EControllerActionOrigin_SteamDeck_Reserved11,
    k_EControllerActionOrigin_SteamDeck_Reserved12,
    k_EControllerActionOrigin_SteamDeck_Reserved13,
    k_EControllerActionOrigin_SteamDeck_Reserved14,
    k_EControllerActionOrigin_SteamDeck_Reserved15,
    k_EControllerActionOrigin_SteamDeck_Reserved16,
    k_EControllerActionOrigin_SteamDeck_Reserved17,
    k_EControllerActionOrigin_SteamDeck_Reserved18,
    k_EControllerActionOrigin_SteamDeck_Reserved19,
    k_EControllerActionOrigin_SteamDeck_Reserved20,

    k_EControllerActionOrigin_Switch_JoyConButton_N, // With a Horizontal JoyCon this will be Y or what would be Dpad Right when vertical
    k_EControllerActionOrigin_Switch_JoyConButton_E, // X
    k_EControllerActionOrigin_Switch_JoyConButton_S, // A
    k_EControllerActionOrigin_Switch_JoyConButton_W, // B

    k_EControllerActionOrigin_PS5_LeftGrip,
    k_EControllerActionOrigin_PS5_RightGrip,
    k_EControllerActionOrigin_PS5_LeftFn,
    k_EControllerActionOrigin_PS5_RightFn,

    k_EControllerActionOrigin_Count, // If Steam has added support for new controllers origins will go here.
    k_EControllerActionOrigin_MaximumPossibleValue = 32767, // Origins are currently a maximum of 16 bits.
} EControllerActionOrigin;

typedef enum ESteamControllerLEDFlag
{
    k_ESteamControllerLEDFlag_SetColor,
    k_ESteamControllerLEDFlag_RestoreUserDefault
} ESteamControllerLEDFlag;

// ControllerHandle_t is used to refer to a specific controller.
// This handle will consistently identify a controller, even if it is disconnected and re-connected
typedef uint64 ControllerHandle_t;


// These handles are used to refer to a specific in-game action or action set
// All action handles should be queried during initialization for performance reasons
typedef uint64 ControllerActionSetHandle_t;
typedef uint64 ControllerDigitalActionHandle_t;
typedef uint64 ControllerAnalogActionHandle_t;

typedef uint64 UGCQueryHandle_t;
typedef uint64 UGCUpdateHandle_t;


const UGCQueryHandle_t k_UGCQueryHandleInvalid = 0xffffffffffffffffull;
const UGCUpdateHandle_t k_UGCUpdateHandleInvalid = 0xffffffffffffffffull;


// Matching UGC types for queries
typedef enum EUGCMatchingUGCType
{
    k_EUGCMatchingUGCType_Items				 = 0,		// both mtx items and ready-to-use items
    k_EUGCMatchingUGCType_Items_Mtx			 = 1,
    k_EUGCMatchingUGCType_Items_ReadyToUse	 = 2,
    k_EUGCMatchingUGCType_Collections		 = 3,
    k_EUGCMatchingUGCType_Artwork			 = 4,
    k_EUGCMatchingUGCType_Videos			 = 5,
    k_EUGCMatchingUGCType_Screenshots		 = 6,
    k_EUGCMatchingUGCType_AllGuides			 = 7,		// both web guides and integrated guides
    k_EUGCMatchingUGCType_WebGuides			 = 8,
    k_EUGCMatchingUGCType_IntegratedGuides	 = 9,
    k_EUGCMatchingUGCType_UsableInGame		 = 10,		// ready-to-use items and integrated guides
    k_EUGCMatchingUGCType_ControllerBindings = 11,
    k_EUGCMatchingUGCType_GameManagedItems	 = 12,		// game managed items (not managed by users)
    k_EUGCMatchingUGCType_All				 = ~0,		// @note: will only be valid for CreateQueryUserUGCRequest requests
} EUGCMatchingUGCType;

// Different lists of published UGC for a user.
// If the current logged in user is different than the specified user, then some options may not be allowed.
typedef enum EUserUGCList
{
    k_EUserUGCList_Published,
    k_EUserUGCList_VotedOn,
    k_EUserUGCList_VotedUp,
    k_EUserUGCList_VotedDown,
    k_EUserUGCList_WillVoteLater,
    k_EUserUGCList_Favorited,
    k_EUserUGCList_Subscribed,
    k_EUserUGCList_UsedOrPlayed,
    k_EUserUGCList_Followed,
} EUserUGCList;

// Sort order for user published UGC lists (defaults to creation order descending)
typedef enum EUserUGCListSortOrder
{
    k_EUserUGCListSortOrder_CreationOrderDesc,
    k_EUserUGCListSortOrder_CreationOrderAsc,
    k_EUserUGCListSortOrder_TitleAsc,
    k_EUserUGCListSortOrder_LastUpdatedDesc,
    k_EUserUGCListSortOrder_SubscriptionDateDesc,
    k_EUserUGCListSortOrder_VoteScoreDesc,
    k_EUserUGCListSortOrder_ForModeration,
} EUserUGCListSortOrder;

// Combination of sorting and filtering for queries across all UGC
typedef enum EUGCQuery
{
    k_EUGCQuery_RankedByVote								  = 0,
    k_EUGCQuery_RankedByPublicationDate						  = 1,
    k_EUGCQuery_AcceptedForGameRankedByAcceptanceDate		  = 2,
    k_EUGCQuery_RankedByTrend								  = 3,
    k_EUGCQuery_FavoritedByFriendsRankedByPublicationDate	  = 4,
    k_EUGCQuery_CreatedByFriendsRankedByPublicationDate		  = 5,
    k_EUGCQuery_RankedByNumTimesReported					  = 6,
    k_EUGCQuery_CreatedByFollowedUsersRankedByPublicationDate = 7,
    k_EUGCQuery_NotYetRated									  = 8,
    k_EUGCQuery_RankedByTotalVotesAsc						  = 9,
    k_EUGCQuery_RankedByVotesUp								  = 10,
    k_EUGCQuery_RankedByTextSearch							  = 11,
    k_EUGCQuery_RankedByTotalUniqueSubscriptions			  = 12,
    k_EUGCQuery_RankedByPlaytimeTrend						  = 13,
    k_EUGCQuery_RankedByTotalPlaytime						  = 14,
    k_EUGCQuery_RankedByAveragePlaytimeTrend				  = 15,
    k_EUGCQuery_RankedByLifetimeAveragePlaytime				  = 16,
    k_EUGCQuery_RankedByPlaytimeSessionsTrend				  = 17,
    k_EUGCQuery_RankedByLifetimePlaytimeSessions			  = 18,
    k_EUGCQuery_RankedByLastUpdatedDate						  = 19,
} EUGCQuery;

typedef enum EItemUpdateStatus
{
    k_EItemUpdateStatusInvalid 				= 0, // The item update handle was invalid, job might be finished, listen too SubmitItemUpdateResult_t
    k_EItemUpdateStatusPreparingConfig 		= 1, // The item update is processing configuration data
    k_EItemUpdateStatusPreparingContent		= 2, // The item update is reading and processing content files
    k_EItemUpdateStatusUploadingContent		= 3, // The item update is uploading content changes to Steam
    k_EItemUpdateStatusUploadingPreviewFile	= 4, // The item update is uploading new preview file image
    k_EItemUpdateStatusCommittingChanges	= 5  // The item update is committing all changes
} EItemUpdateStatus;

typedef enum EItemState
{
    k_EItemStateNone			= 0,	// item not tracked on client
    k_EItemStateSubscribed		= 1,	// current user is subscribed to this item. Not just cached.
    k_EItemStateLegacyItem		= 2,	// item was created with ISteamRemoteStorage
    k_EItemStateInstalled		= 4,	// item is installed and usable (but maybe out of date)
    k_EItemStateNeedsUpdate		= 8,	// items needs an update. Either because it's not installed yet or creator updated content
    k_EItemStateDownloading		= 16,	// item update is currently downloading
    k_EItemStateDownloadPending	= 32,	// DownloadItem() was called for this item, content isn't available until DownloadItemResult_t is fired
    k_EItemStateDisabledLocally = 64,	// Item is disabled locally, so it shouldn't be considered subscribed
} EItemState;

typedef enum EItemStatistic
{
    k_EItemStatistic_NumSubscriptions					 = 0,
    k_EItemStatistic_NumFavorites						 = 1,
    k_EItemStatistic_NumFollowers						 = 2,
    k_EItemStatistic_NumUniqueSubscriptions				 = 3,
    k_EItemStatistic_NumUniqueFavorites					 = 4,
    k_EItemStatistic_NumUniqueFollowers					 = 5,
    k_EItemStatistic_NumUniqueWebsiteViews				 = 6,
    k_EItemStatistic_ReportScore						 = 7,
    k_EItemStatistic_NumSecondsPlayed					 = 8,
    k_EItemStatistic_NumPlaytimeSessions				 = 9,
    k_EItemStatistic_NumComments						 = 10,
    k_EItemStatistic_NumSecondsPlayedDuringTimePeriod	 = 11,
    k_EItemStatistic_NumPlaytimeSessionsDuringTimePeriod = 12,
} EItemStatistic;

typedef enum EItemPreviewType
{
    k_EItemPreviewType_Image							= 0,	// standard image file expected (e.g. jpg, png, gif, etc.)
    k_EItemPreviewType_YouTubeVideo						= 1,	// video id is stored
    k_EItemPreviewType_Sketchfab						= 2,	// model id is stored
    k_EItemPreviewType_EnvironmentMap_HorizontalCross	= 3,	// standard image file expected - cube map in the layout
    // +---+---+-------+
    // |   |Up |       |
    // +---+---+---+---+
    // | L | F | R | B |
    // +---+---+---+---+
    // |   |Dn |       |
    // +---+---+---+---+
    k_EItemPreviewType_EnvironmentMap_LatLong			= 4,	// standard image file expected
    k_EItemPreviewType_Clip								= 5,	// clip id is stored
    k_EItemPreviewType_ReservedMax						= 255,	// you can specify your own types above this value
} EItemPreviewType;

typedef enum EUGCContentDescriptorID
{
    k_EUGCContentDescriptor_NudityOrSexualContent	= 1,
    k_EUGCContentDescriptor_FrequentViolenceOrGore	= 2,
    k_EUGCContentDescriptor_AdultOnlySexualContent	= 3,
    k_EUGCContentDescriptor_GratuitousSexualContent = 4,
    k_EUGCContentDescriptor_AnyMatureContent		= 5,
} EUGCContentDescriptorID;

const uint32 k_cchPublishedDocumentTitleMax = 128 + 1;
const uint32 k_cchPublishedDocumentDescriptionMax = 8000;
const uint32 k_cchTagListMax = 1024 + 1;
const uint32 k_cchFilenameMax = 260;
const uint32 k_cchPublishedFileURLMax = 256;

// Details for a single published file/UGC
typedef struct SteamUGCDetails_t
{
    PublishedFileId_t m_nPublishedFileId;
    EResult m_eResult;												// The result of the operation.
    EWorkshopFileType m_eFileType;									// Type of the file
    AppId_t m_nCreatorAppID;										// ID of the app that created this file.
    AppId_t m_nConsumerAppID;										// ID of the app that will consume this file.
    char m_rgchTitle[k_cchPublishedDocumentTitleMax];				// title of document
    char m_rgchDescription[k_cchPublishedDocumentDescriptionMax];	// description of document
    uint64 m_ulSteamIDOwner;										// Steam ID of the user who created this content.
    uint32 m_rtimeCreated;											// time when the published file was created
    uint32 m_rtimeUpdated;											// time when the published file was last updated
    uint32 m_rtimeAddedToUserList;									// time when the user added the published file to their list (not always applicable)
    ERemoteStoragePublishedFileVisibility m_eVisibility;			// visibility
    bool m_bBanned;													// whether the file was banned
    bool m_bAcceptedForUse;											// developer has specifically flagged this item as accepted in the Workshop
    bool m_bTagsTruncated;											// whether the list of tags was too long to be returned in the provided buffer
    char m_rgchTags[k_cchTagListMax];								// comma separated list of all tags associated with this file
    // file/url information
    UGCHandle_t m_hFile;											// The handle of the primary file
    UGCHandle_t m_hPreviewFile;										// The handle of the preview file
    char m_pchFileName[k_cchFilenameMax];							// The cloud filename of the primary file
    int32 m_nFileSize;												// Size of the primary file (for legacy items which only support one file). This may not be accurate for non-legacy items which can be greater than 4gb in size.
    int32 m_nPreviewFileSize;										// Size of the preview file
    char m_rgchURL[k_cchPublishedFileURLMax];						// URL (for a video or a website)
    // voting information
    uint32 m_unVotesUp;												// number of votes up
    uint32 m_unVotesDown;											// number of votes down
    float m_flScore;												// calculated score
    // collection details
    uint32 m_unNumChildren;
    uint64 m_ulTotalFilesSize;										// Total size of all files (non-legacy), excluding the preview file
} SteamUGCDetails_t;

typedef uint32 HHTMLBrowser;
const uint32 INVALID_HTMLBROWSER = 0;

typedef enum EHTMLMouseButton
{
    eHTMLMouseButton_Left = 0,
    eHTMLMouseButton_Right = 1,
    eHTMLMouseButton_Middle = 2,
} EHTMLMouseButton;

typedef enum EHTMLKeyModifiers
{
    k_eHTMLKeyModifier_None = 0,
    k_eHTMLKeyModifier_AltDown = 1 << 0,
    k_eHTMLKeyModifier_CtrlDown = 1 << 1,
    k_eHTMLKeyModifier_ShiftDown = 1 << 2,
} EHTMLKeyModifiers;

// Every individual instance of an item has a globally-unique ItemInstanceID.
// This ID is unique to the combination of (player, specific item instance)
// and will not be transferred to another player or re-used for another item.
typedef uint64 SteamItemInstanceID_t;

static const SteamItemInstanceID_t k_SteamItemInstanceIDInvalid = (SteamItemInstanceID_t)~0;

// Types of items in your game are identified by a 32-bit "item definition number".
// Valid definition numbers are between 1 and 999999999; numbers less than or equal to
// zero are invalid, and numbers greater than or equal to one billion (1x10^9) are
// reserved for internal Steam use.
typedef int32 SteamItemDef_t;

typedef enum ESteamItemFlags
{
    // Item status flags - these flags are permanently attached to specific item instances
    k_ESteamItemNoTrade = 1 << 0, // This item is account-locked and cannot be traded or given away.

    // Action confirmation flags - these flags are set one time only, as part of a result set
    k_ESteamItemRemoved = 1 << 8,	// The item has been destroyed, traded away, expired, or otherwise invalidated
    k_ESteamItemConsumed = 1 << 9,	// The item quantity has been decreased by 1 via ConsumeItem API.

    // All other flag bits are currently reserved for internal Steam use at this time.
    // Do not assume anything about the state of other flags which are not defined here.
} ESteamItemFlags;

typedef struct SteamItemDetails_t
{
    SteamItemInstanceID_t m_itemId;
    SteamItemDef_t m_iDefinition;
    uint16 m_unQuantity;
    uint16 m_unFlags; // see ESteamItemFlags
} SteamItemDetails_t;

typedef int32 SteamInventoryResult_t;

static const SteamInventoryResult_t k_SteamInventoryResultInvalid = -1;

typedef uint64 SteamInventoryUpdateHandle_t;
const SteamInventoryUpdateHandle_t k_SteamInventoryUpdateHandleInvalid = 0xffffffffffffffffull;

// Controls the color of the timeline bar segments. The value names listed here map to a multiplayer game, where
// the user starts a game (in menus), then joins a multiplayer session that first has a character selection lobby
// then finally the multiplayer session starts. However, you can also map these values to any type of game. In a single
// player game where you visit towns & dungeons, you could set k_ETimelineGameMode_Menus when the player is in a town
// buying items, k_ETimelineGameMode_Staging for when a dungeon is loading and k_ETimelineGameMode_Playing for when
// inside the dungeon fighting monsters.
typedef enum ETimelineGameMode
{
    k_ETimelineGameMode_Invalid = 0,
    k_ETimelineGameMode_Playing = 1,
    k_ETimelineGameMode_Staging = 2,
    k_ETimelineGameMode_Menus = 3,
    k_ETimelineGameMode_LoadingScreen = 4,

    k_ETimelineGameMode_Max, // one past the last valid value
} ETimelineGameMode;

// Used in AddTimelineEvent, where Featured events will be offered before Standard events
typedef enum ETimelineEventClipPriority
{
    k_ETimelineEventClipPriority_Invalid = 0,
    k_ETimelineEventClipPriority_None = 1,
    k_ETimelineEventClipPriority_Standard = 2,
    k_ETimelineEventClipPriority_Featured = 3,
} ETimelineEventClipPriority;

// Feature types for parental settings
typedef enum EParentalFeature
{
    k_EFeatureInvalid = 0,
    k_EFeatureStore = 1,
    k_EFeatureCommunity = 2,
    k_EFeatureProfile = 3,
    k_EFeatureFriends = 4,
    k_EFeatureNews = 5,
    k_EFeatureTrading = 6,
    k_EFeatureSettings = 7,
    k_EFeatureConsole = 8,
    k_EFeatureBrowser = 9,
    k_EFeatureParentalSetup = 10,
    k_EFeatureLibrary = 11,
    k_EFeatureTest = 12,
    k_EFeatureSiteLicense = 13,
    k_EFeatureKioskMode_Deprecated = 14,
    k_EFeatureBlockAlways = 15,
    k_EFeatureMax
} EParentalFeature;

//-----------------------------------------------------------------------------
// Purpose: The form factor of a device
//-----------------------------------------------------------------------------
typedef enum ESteamDeviceFormFactor
{
    k_ESteamDeviceFormFactorUnknown		= 0,
    k_ESteamDeviceFormFactorPhone		= 1,
    k_ESteamDeviceFormFactorTablet		= 2,
    k_ESteamDeviceFormFactorComputer	= 3,
    k_ESteamDeviceFormFactorTV			= 4,
    k_ESteamDeviceFormFactorVRHeadset	= 5,
} ESteamDeviceFormFactor;

// Steam Remote Play session ID
typedef uint32 RemotePlaySessionID_t;

/// High level connection status
typedef enum ESteamNetworkingConnectionState
{

    /// Dummy value used to indicate an error condition in the API.
    /// Specified connection doesn't exist or has already been closed.
    k_ESteamNetworkingConnectionState_None = 0,

    /// We are trying to establish whether peers can talk to each other,
    /// whether they WANT to talk to each other, perform basic auth,
    /// and exchange crypt keys.
    ///
    /// - For connections on the "client" side (initiated locally):
    ///   We're in the process of trying to establish a connection.
    ///   Depending on the connection type, we might not know who they are.
    ///   Note that it is not possible to tell if we are waiting on the
    ///   network to complete handshake packets, or for the application layer
    ///   to accept the connection.
    ///
    /// - For connections on the "server" side (accepted through listen socket):
    ///   We have completed some basic handshake and the client has presented
    ///   some proof of identity.  The connection is ready to be accepted
    ///   using AcceptConnection().
    ///
    /// In either case, any unreliable packets sent now are almost certain
    /// to be dropped.  Attempts to receive packets are guaranteed to fail.
    /// You may send messages if the send mode allows for them to be queued.
    /// but if you close the connection before the connection is actually
    /// established, any queued messages will be discarded immediately.
    /// (We will not attempt to flush the queue and confirm delivery to the
    /// remote host, which ordinarily happens when a connection is closed.)
    k_ESteamNetworkingConnectionState_Connecting = 1,

    /// Some connection types use a back channel or trusted 3rd party
    /// for earliest communication.  If the server accepts the connection,
    /// then these connections switch into the rendezvous state.  During this
    /// state, we still have not yet established an end-to-end route (through
    /// the relay network), and so if you send any messages unreliable, they
    /// are going to be discarded.
    k_ESteamNetworkingConnectionState_FindingRoute = 2,

    /// We've received communications from our peer (and we know
    /// who they are) and are all good.  If you close the connection now,
    /// we will make our best effort to flush out any reliable sent data that
    /// has not been acknowledged by the peer.  (But note that this happens
    /// from within the application process, so unlike a TCP connection, you are
    /// not totally handing it off to the operating system to deal with it.)
    k_ESteamNetworkingConnectionState_Connected = 3,

    /// Connection has been closed by our peer, but not closed locally.
    /// The connection still exists from an API perspective.  You must close the
    /// handle to free up resources.  If there are any messages in the inbound queue,
    /// you may retrieve them.  Otherwise, nothing may be done with the connection
    /// except to close it.
    ///
    /// This stats is similar to CLOSE_WAIT in the TCP state machine.
    k_ESteamNetworkingConnectionState_ClosedByPeer = 4,

    /// A disruption in the connection has been detected locally.  (E.g. timeout,
    /// local internet connection disrupted, etc.)
    ///
    /// The connection still exists from an API perspective.  You must close the
    /// handle to free up resources.
    ///
    /// Attempts to send further messages will fail.  Any remaining received messages
    /// in the queue are available.
    k_ESteamNetworkingConnectionState_ProblemDetectedLocally = 5,

//
// The following values are used internally and will not be returned by any API.
// We document them here to provide a little insight into the state machine that is used
// under the hood.
//

    /// We've disconnected on our side, and from an API perspective the connection is closed.
    /// No more data may be sent or received.  All reliable data has been flushed, or else
    /// we've given up and discarded it.  We do not yet know for sure that the peer knows
    /// the connection has been closed, however, so we're just hanging around so that if we do
    /// get a packet from them, we can send them the appropriate packets so that they can
    /// know why the connection was closed (and not have to rely on a timeout, which makes
    /// it appear as if something is wrong).
    k_ESteamNetworkingConnectionState_FinWait = -1,

    /// We've disconnected on our side, and from an API perspective the connection is closed.
    /// No more data may be sent or received.  From a network perspective, however, on the wire,
    /// we have not yet given any indication to the peer that the connection is closed.
    /// We are in the process of flushing out the last bit of reliable data.  Once that is done,
    /// we will inform the peer that the connection has been closed, and transition to the
    /// FinWait state.
    ///
    /// Note that no indication is given to the remote host that we have closed the connection,
    /// until the data has been flushed.  If the remote host attempts to send us data, we will
    /// do whatever is necessary to keep the connection alive until it can be closed properly.
    /// But in fact the data will be discarded, since there is no way for the application to
    /// read it back.  Typically this is not a problem, as application protocols that utilize
    /// the lingering functionality are designed for the remote host to wait for the response
    /// before sending any more data.
    k_ESteamNetworkingConnectionState_Linger = -2,

    /// Connection is completely inactive and ready to be destroyed
    k_ESteamNetworkingConnectionState_Dead = -3,

    k_ESteamNetworkingConnectionState__Force32Bit = 0x7fffffff
} ESteamNetworkingConnectionState;

/// Enumerate various causes of connection termination.  These are designed to work similar
/// to HTTP error codes: the numeric range gives you a rough classification as to the source
/// of the problem.
typedef enum ESteamNetConnectionEnd
{
    // Invalid/sentinel value
    k_ESteamNetConnectionEnd_Invalid = 0,

    //
    // Application codes.  These are the values you will pass to
    // ISteamNetworkingSockets::CloseConnection.  You can use these codes if
    // you want to plumb through application-specific reason codes.  If you don't
    // need this facility, feel free to always pass
    // k_ESteamNetConnectionEnd_App_Generic.
    //
    // The distinction between "normal" and "exceptional" termination is
    // one you may use if you find useful, but it's not necessary for you
    // to do so.  The only place where we distinguish between normal and
    // exceptional is in connection analytics.  If a significant
    // proportion of connections terminates in an exceptional manner,
    // this can trigger an alert.
    //

    // 1xxx: Application ended the connection in a "usual" manner.
    //       E.g.: user intentionally disconnected from the server,
    //             gameplay ended normally, etc
    k_ESteamNetConnectionEnd_App_Min = 1000,
    k_ESteamNetConnectionEnd_App_Generic = k_ESteamNetConnectionEnd_App_Min,
    // Use codes in this range for "normal" disconnection
    k_ESteamNetConnectionEnd_App_Max = 1999,

    // 2xxx: Application ended the connection in some sort of exceptional
    //       or unusual manner that might indicate a bug or configuration
    //       issue.
    //
    k_ESteamNetConnectionEnd_AppException_Min = 2000,
    k_ESteamNetConnectionEnd_AppException_Generic = k_ESteamNetConnectionEnd_AppException_Min,
    // Use codes in this range for "unusual" disconnection
    k_ESteamNetConnectionEnd_AppException_Max = 2999,

    //
    // System codes.  These will be returned by the system when
    // the connection state is k_ESteamNetworkingConnectionState_ClosedByPeer
    // or k_ESteamNetworkingConnectionState_ProblemDetectedLocally.  It is
    // illegal to pass a code in this range to ISteamNetworkingSockets::CloseConnection
    //

    // 3xxx: Connection failed or ended because of problem with the
    //       local host or their connection to the Internet.
    k_ESteamNetConnectionEnd_Local_Min = 3000,

    // You cannot do what you want to do because you're running in offline mode.
    k_ESteamNetConnectionEnd_Local_OfflineMode = 3001,

    // We're having trouble contacting many (perhaps all) relays.
    // Since it's unlikely that they all went offline at once, the best
    // explanation is that we have a problem on our end.  Note that we don't
    // bother distinguishing between "many" and "all", because in practice,
    // it takes time to detect a connection problem, and by the time
    // the connection has timed out, we might not have been able to
    // actively probe all of the relay clusters, even if we were able to
    // contact them at one time.  So this code just means that:
    //
    // * We don't have any recent successful communication with any relay.
    // * We have evidence of recent failures to communicate with multiple relays.
    k_ESteamNetConnectionEnd_Local_ManyRelayConnectivity = 3002,

    // A hosted server is having trouble talking to the relay
    // that the client was using, so the problem is most likely
    // on our end
    k_ESteamNetConnectionEnd_Local_HostedServerPrimaryRelay = 3003,

    // We're not able to get the SDR network config.  This is
    // *almost* always a local issue, since the network config
    // comes from the CDN, which is pretty darn reliable.
    k_ESteamNetConnectionEnd_Local_NetworkConfig = 3004,

    // Steam rejected our request because we don't have rights
    // to do this.
    k_ESteamNetConnectionEnd_Local_Rights = 3005,

    // ICE P2P rendezvous failed because we were not able to
    // determine our "public" address (e.g. reflexive address via STUN)
    //
    // If relay fallback is available (it always is on Steam), then
    // this is only used internally and will not be returned as a high
    // level failure.
    k_ESteamNetConnectionEnd_Local_P2P_ICE_NoPublicAddresses = 3006,

    k_ESteamNetConnectionEnd_Local_Max = 3999,

    // 4xxx: Connection failed or ended, and it appears that the
    //       cause does NOT have to do with the local host or their
    //       connection to the Internet.  It could be caused by the
    //       remote host, or it could be somewhere in between.
    k_ESteamNetConnectionEnd_Remote_Min = 4000,

    // The connection was lost, and as far as we can tell our connection
    // to relevant services (relays) has not been disrupted.  This doesn't
    // mean that the problem is "their fault", it just means that it doesn't
    // appear that we are having network issues on our end.
    k_ESteamNetConnectionEnd_Remote_Timeout = 4001,

    // Something was invalid with the cert or crypt handshake
    // info you gave me, I don't understand or like your key types,
    // etc.
    k_ESteamNetConnectionEnd_Remote_BadCrypt = 4002,

    // You presented me with a cert that was I was able to parse
    // and *technically* we could use encrypted communication.
    // But there was a problem that prevents me from checking your identity
    // or ensuring that somebody int he middle can't observe our communication.
    // E.g.: - the CA key was missing (and I don't accept unsigned certs)
    // - The CA key isn't one that I trust,
    // - The cert doesn't was appropriately restricted by app, user, time, data center, etc.
    // - The cert wasn't issued to you.
    // - etc
    k_ESteamNetConnectionEnd_Remote_BadCert = 4003,

    // These will never be returned
    //k_ESteamNetConnectionEnd_Remote_NotLoggedIn_DEPRECATED = 4004,
    //k_ESteamNetConnectionEnd_Remote_NotRunningApp_DEPRECATED = 4005,

    // Something wrong with the protocol version you are using.
    // (Probably the code you are running is too old.)
    k_ESteamNetConnectionEnd_Remote_BadProtocolVersion = 4006,

    // NAT punch failed failed because we never received any public
    // addresses from the remote host.  (But we did receive some
    // signals form them.)
    //
    // If relay fallback is available (it always is on Steam), then
    // this is only used internally and will not be returned as a high
    // level failure.
    k_ESteamNetConnectionEnd_Remote_P2P_ICE_NoPublicAddresses = 4007,

    k_ESteamNetConnectionEnd_Remote_Max = 4999,

    // 5xxx: Connection failed for some other reason.
    k_ESteamNetConnectionEnd_Misc_Min = 5000,

    // A failure that isn't necessarily the result of a software bug,
    // but that should happen rarely enough that it isn't worth specifically
    // writing UI or making a localized message for.
    // The debug string should contain further details.
    k_ESteamNetConnectionEnd_Misc_Generic = 5001,

    // Generic failure that is most likely a software bug.
    k_ESteamNetConnectionEnd_Misc_InternalError = 5002,

    // The connection to the remote host timed out, but we
    // don't know if the problem is on our end, in the middle,
    // or on their end.
    k_ESteamNetConnectionEnd_Misc_Timeout = 5003,

    //k_ESteamNetConnectionEnd_Misc_RelayConnectivity_DEPRECATED = 5004,

    // There's some trouble talking to Steam.
    k_ESteamNetConnectionEnd_Misc_SteamConnectivity = 5005,

    // A server in a dedicated hosting situation has no relay sessions
    // active with which to talk back to a client.  (It's the client's
    // job to open and maintain those sessions.)
    k_ESteamNetConnectionEnd_Misc_NoRelaySessionsToClient = 5006,

    // While trying to initiate a connection, we never received
    // *any* communication from the peer.
    //k_ESteamNetConnectionEnd_Misc_ServerNeverReplied = 5007,

    // P2P rendezvous failed in a way that we don't have more specific
    // information
    k_ESteamNetConnectionEnd_Misc_P2P_Rendezvous = 5008,

    // NAT punch failed, probably due to NAT/firewall configuration.
    //
    // If relay fallback is available (it always is on Steam), then
    // this is only used internally and will not be returned as a high
    // level failure.
    k_ESteamNetConnectionEnd_Misc_P2P_NAT_Firewall = 5009,

    // Our peer replied that it has no record of the connection.
    // This should not happen ordinarily, but can happen in a few
    // exception cases:
    //
    // - This is an old connection, and the peer has already cleaned
    //   up and forgotten about it.  (Perhaps it timed out and they
    //   closed it and were not able to communicate this to us.)
    // - A bug or internal protocol error has caused us to try to
    //   talk to the peer about the connection before we received
    //   confirmation that the peer has accepted the connection.
    // - The peer thinks that we have closed the connection for some
    //   reason (perhaps a bug), and believes that is it is
    //   acknowledging our closure.
    k_ESteamNetConnectionEnd_Misc_PeerSentNoConnection = 5010,

    k_ESteamNetConnectionEnd_Misc_Max = 5999,

    k_ESteamNetConnectionEnd__Force32Bit = 0x7fffffff
} ESteamNetConnectionEnd;

/// Handle used to identify a connection to a remote host.
typedef uint32 HSteamNetConnection;
const HSteamNetConnection k_HSteamNetConnection_Invalid = 0;

/// Handle used to identify a "listen socket".  Unlike traditional
/// Berkeley sockets, a listen socket and a connection are two
/// different abstractions.
typedef uint32 HSteamListenSocket;
const HSteamListenSocket k_HSteamListenSocket_Invalid = 0;

/// Handle used to identify a poll group, used to query many
/// connections at once efficiently.
typedef uint32 HSteamNetPollGroup;
const HSteamNetPollGroup k_HSteamNetPollGroup_Invalid = 0;

/// Max length of diagnostic error message
const int k_cchMaxSteamNetworkingErrMsg = 1024;

/// Used to return English-language diagnostic error messages to caller.
/// (For debugging or spewing to a console, etc.  Not intended for UI.)
typedef char SteamNetworkingErrMsg[ k_cchMaxSteamNetworkingErrMsg ];

/// Identifier used for a network location point of presence.  (E.g. a Valve data center.)
/// Typically you won't need to directly manipulate these.
typedef uint32 SteamNetworkingPOPID;

/// A local timestamp.  You can subtract two timestamps to get the number of elapsed
/// microseconds.  This is guaranteed to increase over time during the lifetime
/// of a process, but not globally across runs.  You don't need to worry about
/// the value wrapping around.  Note that the underlying clock might not actually have
/// microsecond resolution.
typedef int64 SteamNetworkingMicroseconds;

/// Describe the status of a particular network resource
typedef enum ESteamNetworkingAvailability
{
    // Negative values indicate a problem.
    //
    // In general, we will not automatically retry unless you take some action that
    // depends on of requests this resource, such as querying the status, attempting
    // to initiate a connection, receive a connection, etc.  If you do not take any
    // action at all, we do not automatically retry in the background.
    k_ESteamNetworkingAvailability_CannotTry = -102,		// A dependent resource is missing, so this service is unavailable.  (E.g. we cannot talk to routers because Internet is down or we don't have the network config.)
    k_ESteamNetworkingAvailability_Failed = -101,			// We have tried for enough time that we would expect to have been successful by now.  We have never been successful
    k_ESteamNetworkingAvailability_Previously = -100,		// We tried and were successful at one time, but now it looks like we have a problem

    k_ESteamNetworkingAvailability_Retrying = -10,		// We previously failed and are currently retrying

    // Not a problem, but not ready either
    k_ESteamNetworkingAvailability_NeverTried = 1,		// We don't know because we haven't ever checked/tried
    k_ESteamNetworkingAvailability_Waiting = 2,			// We're waiting on a dependent resource to be acquired.  (E.g. we cannot obtain a cert until we are logged into Steam.  We cannot measure latency to relays until we have the network config.)
    k_ESteamNetworkingAvailability_Attempting = 3,		// We're actively trying now, but are not yet successful.

    k_ESteamNetworkingAvailability_Current = 100,			// Resource is online/available


    k_ESteamNetworkingAvailability_Unknown = 0,			// Internal dummy/sentinel, or value is not applicable in this context
    k_ESteamNetworkingAvailability__Force32bit = 0x7fffffff,
} ESteamNetworkingAvailability;

/// Detail level for diagnostic output callback.
/// See ISteamNetworkingUtils::SetDebugOutputFunction
typedef enum ESteamNetworkingSocketsDebugOutputType
{
    k_ESteamNetworkingSocketsDebugOutputType_None = 0,
    k_ESteamNetworkingSocketsDebugOutputType_Bug = 1, // You used the API incorrectly, or an internal error happened
    k_ESteamNetworkingSocketsDebugOutputType_Error = 2, // Run-time error condition that isn't the result of a bug.  (E.g. we are offline, cannot bind a port, etc)
    k_ESteamNetworkingSocketsDebugOutputType_Important = 3, // Nothing is wrong, but this is an important notification
    k_ESteamNetworkingSocketsDebugOutputType_Warning = 4,
    k_ESteamNetworkingSocketsDebugOutputType_Msg = 5, // Recommended amount
    k_ESteamNetworkingSocketsDebugOutputType_Verbose = 6, // Quite a bit
    k_ESteamNetworkingSocketsDebugOutputType_Debug = 7, // Practically everything
    k_ESteamNetworkingSocketsDebugOutputType_Everything = 8, // Wall of text, detailed packet contents breakdown, etc

    k_ESteamNetworkingSocketsDebugOutputType__Force32Bit = 0x7fffffff
} ESteamNetworkingSocketsDebugOutputType;

/// Setup callback for debug output, and the desired verbosity you want.
typedef void (*FSteamNetworkingSocketsDebugOutput)( ESteamNetworkingSocketsDebugOutputType nType, const char *pszMsg );

/// "Fake IPs" are assigned to hosts, to make it easier to interface with
/// older code that assumed all hosts will have an IPv4 address
typedef enum ESteamNetworkingFakeIPType
{
    k_ESteamNetworkingFakeIPType_Invalid, // Error, argument was not even an IP address, etc.
    k_ESteamNetworkingFakeIPType_NotFake, // Argument was a valid IP, but was not from the reserved "fake" range
    k_ESteamNetworkingFakeIPType_GlobalIPv4, // Globally unique (for a given app) IPv4 address.  Address space managed by Steam
    k_ESteamNetworkingFakeIPType_LocalIPv4, // Locally unique IPv4 address.  Address space managed by the local process.  For internal use only; should not be shared!

    k_ESteamNetworkingFakeIPType__Force32Bit = 0x7fffffff
} ESteamNetworkingFakeIPType;

/// Configuration values can be applied to different types of objects.
typedef enum ESteamNetworkingConfigScope
{

    /// Get/set global option, or defaults.  Even options that apply to more specific scopes
    /// have global scope, and you may be able to just change the global defaults.  If you
    /// need different settings per connection (for example), then you will need to set those
    /// options at the more specific scope.
    k_ESteamNetworkingConfig_Global = 1,

    /// Some options are specific to a particular interface.  Note that all connection
    /// and listen socket settings can also be set at the interface level, and they will
    /// apply to objects created through those interfaces.
    k_ESteamNetworkingConfig_SocketsInterface = 2,

    /// Options for a listen socket.  Listen socket options can be set at the interface layer,
    /// if  you have multiple listen sockets and they all use the same options.
    /// You can also set connection options on a listen socket, and they set the defaults
    /// for all connections accepted through this listen socket.  (They will be used if you don't
    /// set a connection option.)
    k_ESteamNetworkingConfig_ListenSocket = 3,

    /// Options for a specific connection.
    k_ESteamNetworkingConfig_Connection = 4,

    k_ESteamNetworkingConfigScope__Force32Bit = 0x7fffffff
} ESteamNetworkingConfigScope;

// Different configuration values have different data types
typedef enum ESteamNetworkingConfigDataType
{
    k_ESteamNetworkingConfig_Int32 = 1,
    k_ESteamNetworkingConfig_Int64 = 2,
    k_ESteamNetworkingConfig_Float = 3,
    k_ESteamNetworkingConfig_String = 4,
    k_ESteamNetworkingConfig_Ptr = 5,

    k_ESteamNetworkingConfigDataType__Force32Bit = 0x7fffffff
} ESteamNetworkingConfigDataType;

/// Configuration options
typedef enum ESteamNetworkingConfigValue
{
    k_ESteamNetworkingConfig_Invalid = 0,

//
// Connection options
//

    /// [connection int32] Timeout value (in ms) to use when first connecting
    k_ESteamNetworkingConfig_TimeoutInitial = 24,

    /// [connection int32] Timeout value (in ms) to use after connection is established
    k_ESteamNetworkingConfig_TimeoutConnected = 25,

    /// [connection int32] Upper limit of buffered pending bytes to be sent,
    /// if this is reached SendMessage will return k_EResultLimitExceeded
    /// Default is 512k (524288 bytes)
    k_ESteamNetworkingConfig_SendBufferSize = 9,

    /// [connection int32] Upper limit on total size (in bytes) of received messages
    /// that will be buffered waiting to be processed by the application.  If this limit
    /// is exceeded, packets will be dropped.  This is to protect us from a malicious
    /// peer flooding us with messages faster than we can process them.
    ///
    /// This must be bigger than k_ESteamNetworkingConfig_RecvMaxMessageSize
    k_ESteamNetworkingConfig_RecvBufferSize = 47,

    /// [connection int32] Upper limit on the number of received messages that will
    /// that will be buffered waiting to be processed by the application.  If this limit
    /// is exceeded, packets will be dropped.  This is to protect us from a malicious
    /// peer flooding us with messages faster than we can pull them off the wire.
    k_ESteamNetworkingConfig_RecvBufferMessages = 48,

    /// [connection int32] Maximum message size that we are willing to receive.
    /// if a client attempts to send us a message larger than this, the connection
    /// will be immediately closed.
    ///
    /// Default is 512k (524288 bytes).  Note that the peer needs to be able to
    /// send a message this big.  (See k_cbMaxSteamNetworkingSocketsMessageSizeSend.)
    k_ESteamNetworkingConfig_RecvMaxMessageSize = 49,

    /// [connection int32] Max number of message segments that can be received
    /// in a single UDP packet.  While decoding a packet, if the number of segments
    /// exceeds this, we will abort further packet processing.
    ///
    /// The default is effectively unlimited.  If you know that you very rarely
    /// send small packets, you can protect yourself from malicious senders by
    /// lowering this number.
    ///
    /// In particular, if you are NOT using the reliability layer and are only using
    /// SteamNetworkingSockets for datagram transport, setting this to a very low
    /// number may be beneficial.  (We recommend a value of 2.)  Make sure your sender
    /// disables Nagle!
    k_ESteamNetworkingConfig_RecvMaxSegmentsPerPacket = 50,

    /// [connection int64] Get/set userdata as a configuration option.
    /// The default value is -1.   You may want to set the user data as
    /// a config value, instead of using ISteamNetworkingSockets::SetConnectionUserData
    /// in two specific instances:
    ///
    /// - You wish to set the userdata atomically when creating
    ///   an outbound connection, so that the userdata is filled in properly
    ///   for any callbacks that happen.  However, note that this trick
    ///   only works for connections initiated locally!  For incoming
    ///   connections, multiple state transitions may happen and
    ///   callbacks be queued, before you are able to service the first
    ///   callback!  Be careful!
    ///
    /// - You can set the default userdata for all newly created connections
    ///   by setting this value at a higher level (e.g. on the listen
    ///   socket or at the global level.)  Then this default
    ///   value will be inherited when the connection is created.
    ///   This is useful in case -1 is a valid userdata value, and you
    ///   wish to use something else as the default value so you can
    ///   tell if it has been set or not.
    ///
    ///   HOWEVER: once a connection is created, the effective value is
    ///   then bound to the connection.  Unlike other connection options,
    ///   if you change it again at a higher level, the new value will not
    ///   be inherited by connections.
    ///
    /// Using the userdata field in callback structs is not advised because
    /// of tricky race conditions.  Instead, you might try one of these methods:
    ///
    /// - Use a separate map with the HSteamNetConnection as the key.
    /// - Fetch the userdata from the connection in your callback
    ///   using ISteamNetworkingSockets::GetConnectionUserData, to
    //    ensure you have the current value.
    k_ESteamNetworkingConfig_ConnectionUserData = 40,

    /// [connection int32] Minimum/maximum send rate clamp, in bytes/sec.
    /// At the time of this writing these two options should always be set to
    /// the same value, to manually configure a specific send rate.  The default
    /// value is 256K.  Eventually we hope to have the library estimate the bandwidth
    /// of the channel and set the send rate to that estimated bandwidth, and these
    /// values will only set limits on that send rate.
    k_ESteamNetworkingConfig_SendRateMin = 10,
    k_ESteamNetworkingConfig_SendRateMax = 11,

    /// [connection int32] Nagle time, in microseconds.  When SendMessage is called, if
    /// the outgoing message is less than the size of the MTU, it will be
    /// queued for a delay equal to the Nagle timer value.  This is to ensure
    /// that if the application sends several small messages rapidly, they are
    /// coalesced into a single packet.
    /// See historical RFC 896.  Value is in microseconds.
    /// Default is 5000us (5ms).
    k_ESteamNetworkingConfig_NagleTime = 12,

    /// [connection int32] Don't automatically fail IP connections that don't have
    /// strong auth.  On clients, this means we will attempt the connection even if
    /// we don't know our identity or can't get a cert.  On the server, it means that
    /// we won't automatically reject a connection due to a failure to authenticate.
    /// (You can examine the incoming connection and decide whether to accept it.)
    ///
    /// 0: Don't attempt or accept unauthorized connections
    /// 1: Attempt authorization when connecting, and allow unauthorized peers, but emit warnings
    /// 2: don't attempt authentication, or complain if peer is unauthenticated
    ///
    /// This is a dev configuration value, and you should not let users modify it in
    /// production.
    k_ESteamNetworkingConfig_IP_AllowWithoutAuth = 23,

    /// [connection int32] The same as IP_AllowWithoutAuth, but will only apply
    /// for connections to/from localhost addresses.  Whichever value is larger
    /// (more permissive) will be used.
    k_ESteamNetworkingConfig_IPLocalHost_AllowWithoutAuth = 52,

    /// [connection int32] Do not send UDP packets with a payload of
    /// larger than N bytes.  If you set this, k_ESteamNetworkingConfig_MTU_DataSize
    /// is automatically adjusted
    k_ESteamNetworkingConfig_MTU_PacketSize = 32,

    /// [connection int32] (read only) Maximum message size you can send that
    /// will not fragment, based on k_ESteamNetworkingConfig_MTU_PacketSize
    k_ESteamNetworkingConfig_MTU_DataSize = 33,

    /// [connection int32] Allow unencrypted (and unauthenticated) communication.
    /// 0: Not allowed (the default)
    /// 1: Allowed, but prefer encrypted
    /// 2: Allowed, and preferred
    /// 3: Required.  (Fail the connection if the peer requires encryption.)
    ///
    /// This is a dev configuration value, since its purpose is to disable encryption.
    /// You should not let users modify it in production.  (But note that it requires
    /// the peer to also modify their value in order for encryption to be disabled.)
    k_ESteamNetworkingConfig_Unencrypted = 34,

    /// [connection int32] Set this to 1 on outbound connections and listen sockets,
    /// to enable "symmetric connect mode", which is useful in the following
    /// common peer-to-peer use case:
    ///
    /// - The two peers are "equal" to each other.  (Neither is clearly the "client"
    ///   or "server".)
    /// - Either peer may initiate the connection, and indeed they may do this
    ///   at the same time
    /// - The peers only desire a single connection to each other, and if both
    ///   peers initiate connections simultaneously, a protocol is needed for them
    ///   to resolve the conflict, so that we end up with a single connection.
    ///
    /// This use case is both common, and involves subtle race conditions and tricky
    /// pitfalls, which is why the API has support for dealing with it.
    ///
    /// If an incoming connection arrives on a listen socket or via custom signaling,
    /// and the application has not attempted to make a matching outbound connection
    /// in symmetric mode, then the incoming connection can be accepted as usual.
    /// A "matching" connection means that the relevant endpoint information matches.
    /// (At the time this comment is being written, this is only supported for P2P
    /// connections, which means that the peer identities must match, and the virtual
    /// port must match.  At a later time, symmetric mode may be supported for other
    /// connection types.)
    ///
    /// If connections are initiated by both peers simultaneously, race conditions
    /// can arise, but fortunately, most of them are handled internally and do not
    /// require any special awareness from the application.  However, there
    /// is one important case that application code must be aware of:
    /// If application code attempts an outbound connection using a ConnectXxx
    /// function in symmetric mode, and a matching incoming connection is already
    /// waiting on a listen socket, then instead of forming a new connection,
    /// the ConnectXxx call will accept the existing incoming connection, and return
    /// a connection handle to this accepted connection.
    /// IMPORTANT: in this case, a SteamNetConnectionStatusChangedCallback_t
    /// has probably *already* been posted to the queue for the incoming connection!
    /// (Once callbacks are posted to the queue, they are not modified.)  It doesn't
    /// matter if the callback has not been consumed by the app.  Thus, application
    /// code that makes use of symmetric connections must be aware that, when processing a
    /// SteamNetConnectionStatusChangedCallback_t for an incoming connection, the
    /// m_hConn may refer to a new connection that the app has has not
    /// seen before (the usual case), but it may also refer to a connection that
    /// has already been accepted implicitly through a call to Connect()!  In this
    /// case, AcceptConnection() will return k_EResultDuplicateRequest.
    ///
    /// Only one symmetric connection to a given peer (on a given virtual port)
    /// may exist at any given time.  If client code attempts to create a connection,
    /// and a (live) connection already exists on the local host, then either the
    /// existing connection will be accepted as described above, or the attempt
    /// to create a new connection will fail.  Furthermore, linger mode functionality
    /// is not supported on symmetric connections.
    ///
    /// A more complicated race condition can arise if both peers initiate a connection
    /// at roughly the same time.  In this situation, each peer will receive an incoming
    /// connection from the other peer, when the application code has already initiated
    /// an outgoing connection to that peer.  The peers must resolve this conflict and
    /// decide who is going to act as the "server" and who will act as the "client".
    /// Typically the application does not need to be aware of this case as it is handled
    /// internally.  On both sides, the will observe their outbound connection being
    /// "accepted", although one of them one have been converted internally to act
    /// as the "server".
    ///
    /// In general, symmetric mode should be all-or-nothing: do not mix symmetric
    /// connections with a non-symmetric connection that it might possible "match"
    /// with.  If you use symmetric mode on any connections, then both peers should
    /// use it on all connections, and the corresponding listen socket, if any.  The
    /// behaviour when symmetric and ordinary connections are mixed is not defined by
    /// this API, and you should not rely on it.  (This advice only applies when connections
    /// might possibly "match".  For example, it's OK to use all symmetric mode
    /// connections on one virtual port, and all ordinary, non-symmetric connections
    /// on a different virtual port, as there is no potential for ambiguity.)
    ///
    /// When using the feature, you should set it in the following situations on
    /// applicable objects:
    ///
    /// - When creating an outbound connection using ConnectXxx function
    /// - When creating a listen socket.  (Note that this will automatically cause
    ///   any accepted connections to inherit the flag.)
    /// - When using custom signaling, before accepting an incoming connection.
    ///
    /// Setting the flag on listen socket and accepted connections will enable the
    /// API to automatically deal with duplicate incoming connections, even if the
    /// local host has not made any outbound requests.  (In general, such duplicate
    /// requests from a peer are ignored internally and will not be visible to the
    /// application code.  The previous connection must be closed or resolved first.)
    k_ESteamNetworkingConfig_SymmetricConnect = 37,

    /// [connection int32] For connection types that use "virtual ports", this can be used
    /// to assign a local virtual port.  For incoming connections, this will always be the
    /// virtual port of the listen socket (or the port requested by the remote host if custom
    /// signaling is used and the connection is accepted), and cannot be changed.  For
    /// connections initiated locally, the local virtual port will default to the same as the
    /// requested remote virtual port, if you do not specify a different option when creating
    /// the connection.  The local port is only relevant for symmetric connections, when
    /// determining if two connections "match."  In this case, if you need the local and remote
    /// port to differ, you can set this value.
    ///
    /// You can also read back this value on listen sockets.
    ///
    /// This value should not be read or written in any other context.
    k_ESteamNetworkingConfig_LocalVirtualPort = 38,

    /// [connection int32] Enable Dual wifi band support for this connection
    /// 0 = no, 1 = yes, 2 = simulate it for debugging, even if dual wifi not available
    k_ESteamNetworkingConfig_DualWifi_Enable = 39,

    /// [connection int32] True to enable diagnostics reporting through
    /// generic platform UI.  (Only available on Steam.)
    k_ESteamNetworkingConfig_EnableDiagnosticsUI = 46,

//
// Simulating network conditions
//
// These are global (not per-connection) because they apply at
// a relatively low UDP layer.
//

    /// [global float, 0--100] Randomly discard N pct of packets instead of sending/recv
    /// This is a global option only, since it is applied at a low level
    /// where we don't have much context
    k_ESteamNetworkingConfig_FakePacketLoss_Send = 2,
    k_ESteamNetworkingConfig_FakePacketLoss_Recv = 3,

    /// [global int32].  Delay all outbound/inbound packets by N ms
    k_ESteamNetworkingConfig_FakePacketLag_Send = 4,
    k_ESteamNetworkingConfig_FakePacketLag_Recv = 5,

    /// [global float] 0-100 Percentage of packets we will add additional delay
    /// to (causing them to be reordered)
    k_ESteamNetworkingConfig_FakePacketReorder_Send = 6,
    k_ESteamNetworkingConfig_FakePacketReorder_Recv = 7,

    /// [global int32] Extra delay, in ms, to apply to reordered packets.
    k_ESteamNetworkingConfig_FakePacketReorder_Time = 8,

    /// [global float 0--100] Globally duplicate some percentage of packets we send
    k_ESteamNetworkingConfig_FakePacketDup_Send = 26,
    k_ESteamNetworkingConfig_FakePacketDup_Recv = 27,

    /// [global int32] Amount of delay, in ms, to delay duplicated packets.
    /// (We chose a random delay between 0 and this value)
    k_ESteamNetworkingConfig_FakePacketDup_TimeMax = 28,

    /// [global int32] Trace every UDP packet, similar to Wireshark or tcpdump.
    /// Value is max number of bytes to dump.  -1 disables tracing.
    // 0 only traces the info but no actual data bytes
    k_ESteamNetworkingConfig_PacketTraceMaxBytes = 41,


    // [global int32] Global UDP token bucket rate limits.
    // "Rate" refers to the steady state rate. (Bytes/sec, the
    // rate that tokens are put into the bucket.)  "Burst"
    // refers to the max amount that could be sent in a single
    // burst.  (In bytes, the max capacity of the bucket.)
    // Rate=0 disables the limiter entirely, which is the default.
    // Burst=0 disables burst.  (This is not realistic.  A
    // burst of at least 4K is recommended; the default is higher.)
    k_ESteamNetworkingConfig_FakeRateLimit_Send_Rate = 42,
    k_ESteamNetworkingConfig_FakeRateLimit_Send_Burst = 43,
    k_ESteamNetworkingConfig_FakeRateLimit_Recv_Rate = 44,
    k_ESteamNetworkingConfig_FakeRateLimit_Recv_Burst = 45,

    // Timeout used for out-of-order correction.  This is used when we see a small
    // gap in the sequence number on a packet flow.  For example let's say we are
    // processing packet 105 when the most recent one was 103.  104 might have dropped,
    // but there is also a chance that packets are simply being reordered.  It is very
    // common on certain types of connections for packet 104 to arrive very soon after 105,
    // especially if 104 was large and 104 was small.  In this case, when we see packet 105
    // we will shunt it aside and pend it, in the hopes of seeing 104 soon after.  If 104
    // arrives before the a timeout occurs, then we can deliver the packets in order to the
    // remainder of packet processing, and we will record this as a "correctable" out-of-order
    // situation.  If the timer expires, then we will process packet 105, and assume for now
    // that 104 has dropped.  (If 104 later arrives, we will process it, but that will be
    // accounted for as uncorrected.)
    //
    // The default value is 1000 microseconds.  Note that the Windows scheduler does not
    // have microsecond precision.
    //
    // Set the value to 0 to disable out of order correction at the packet layer.
    // In many cases we are still effectively able to correct the situation because
    // reassembly of message fragments is tolerant of fragments packets arriving out of
    // order.  Also, when messages are decoded and inserted into the queue for the app
    // to receive them, we will correct out of order messages that have not been
    // dequeued by the app yet.  However, when out-of-order packets are corrected
    // at the packet layer, they will not reduce the connection quality measure.
    // (E.g. SteamNetConnectionRealTimeStatus_t::m_flConnectionQualityLocal)
    k_ESteamNetworkingConfig_OutOfOrderCorrectionWindowMicroseconds = 51,

//
// Callbacks
//

    // On Steam, you may use the default Steam callback dispatch mechanism.  If you prefer
    // to not use this dispatch mechanism (or you are not running with Steam), or you want
    // to associate specific functions with specific listen sockets or connections, you can
    // register them as configuration values.
    //
    // Note also that ISteamNetworkingUtils has some helpers to set these globally.

    /// [connection FnSteamNetConnectionStatusChanged] Callback that will be invoked
    /// when the state of a connection changes.
    ///
    /// IMPORTANT: callbacks are dispatched to the handler that is in effect at the time
    /// the event occurs, which might be in another thread.  For example, immediately after
    /// creating a listen socket, you may receive an incoming connection.  And then immediately
    /// after this, the remote host may close the connection.  All of this could happen
    /// before the function to create the listen socket has returned.  For this reason,
    /// callbacks usually must be in effect at the time of object creation.  This means
    /// you should set them when you are creating the listen socket or connection, or have
    /// them in effect so they will be inherited at the time of object creation.
    ///
    /// For example:
    ///
    /// exterm void MyStatusChangedFunc( SteamNetConnectionStatusChangedCallback_t *info );
    /// SteamNetworkingConfigValue_t opt; opt.SetPtr( k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged, MyStatusChangedFunc );
    /// SteamNetworkingIPAddr localAddress; localAddress.Clear();
    /// HSteamListenSocket hListenSock = SteamNetworkingSockets()->CreateListenSocketIP( localAddress, 1, &opt );
    ///
    /// When accepting an incoming connection, there is no atomic way to switch the
    /// callback.  However, if the connection is DOA, AcceptConnection() will fail, and
    /// you can fetch the state of the connection at that time.
    ///
    /// If all connections and listen sockets can use the same callback, the simplest
    /// method is to set it globally before you create any listen sockets or connections.
    k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged = 201,

    /// [global FnSteamNetAuthenticationStatusChanged] Callback that will be invoked
    /// when our auth state changes.  If you use this, install the callback before creating
    /// any connections or listen sockets, and don't change it.
    /// See: ISteamNetworkingUtils::SetGlobalCallback_SteamNetAuthenticationStatusChanged
    k_ESteamNetworkingConfig_Callback_AuthStatusChanged = 202,

    /// [global FnSteamRelayNetworkStatusChanged] Callback that will be invoked
    /// when our auth state changes.  If you use this, install the callback before creating
    /// any connections or listen sockets, and don't change it.
    /// See: ISteamNetworkingUtils::SetGlobalCallback_SteamRelayNetworkStatusChanged
    k_ESteamNetworkingConfig_Callback_RelayNetworkStatusChanged = 203,

    /// [global FnSteamNetworkingMessagesSessionRequest] Callback that will be invoked
    /// when a peer wants to initiate a SteamNetworkingMessagesSessionRequest.
    /// See: ISteamNetworkingUtils::SetGlobalCallback_MessagesSessionRequest
    k_ESteamNetworkingConfig_Callback_MessagesSessionRequest = 204,

    /// [global FnSteamNetworkingMessagesSessionFailed] Callback that will be invoked
    /// when a session you have initiated, or accepted either fails to connect, or loses
    /// connection in some unexpected way.
    /// See: ISteamNetworkingUtils::SetGlobalCallback_MessagesSessionFailed
    k_ESteamNetworkingConfig_Callback_MessagesSessionFailed = 205,

    /// [global FnSteamNetworkingSocketsCreateConnectionSignaling] Callback that will
    /// be invoked when we need to create a signaling object for a connection
    /// initiated locally.  See: ISteamNetworkingSockets::ConnectP2P,
    /// ISteamNetworkingMessages.
    k_ESteamNetworkingConfig_Callback_CreateConnectionSignaling = 206,

    /// [global FnSteamNetworkingFakeIPResult] Callback that's invoked when
    /// a FakeIP allocation finishes.  See: ISteamNetworkingSockets::BeginAsyncRequestFakeIP,
    /// ISteamNetworkingUtils::SetGlobalCallback_FakeIPResult
    k_ESteamNetworkingConfig_Callback_FakeIPResult = 207,

//
// P2P connection settings
//

//	/// [listen socket int32] When you create a P2P listen socket, we will automatically
//	/// open up a UDP port to listen for LAN connections.  LAN connections can be made
//	/// without any signaling: both sides can be disconnected from the Internet.
//	///
//	/// This value can be set to zero to disable the feature.
//	k_ESteamNetworkingConfig_P2P_Discovery_Server_LocalPort = 101,
//
//	/// [connection int32] P2P connections can perform broadcasts looking for the peer
//	/// on the LAN.
//	k_ESteamNetworkingConfig_P2P_Discovery_Client_RemotePort = 102,

    /// [connection string] Comma-separated list of STUN servers that can be used
    /// for NAT piercing.  If you set this to an empty string, NAT piercing will
    /// not be attempted.  Also if "public" candidates are not allowed for
    /// P2P_Transport_ICE_Enable, then this is ignored.
    k_ESteamNetworkingConfig_P2P_STUN_ServerList = 103,

    /// [connection int32] What types of ICE candidates to share with the peer.
    /// See k_nSteamNetworkingConfig_P2P_Transport_ICE_Enable_xxx values
    k_ESteamNetworkingConfig_P2P_Transport_ICE_Enable = 104,

    /// [connection int32] When selecting P2P transport, add various
    /// penalties to the scores for selected transports.  (Route selection
    /// scores are on a scale of milliseconds.  The score begins with the
    /// route ping time and is then adjusted.)
    k_ESteamNetworkingConfig_P2P_Transport_ICE_Penalty = 105,
    k_ESteamNetworkingConfig_P2P_Transport_SDR_Penalty = 106,
    k_ESteamNetworkingConfig_P2P_TURN_ServerList = 107,
    k_ESteamNetworkingConfig_P2P_TURN_UserList = 108,
    k_ESteamNetworkingConfig_P2P_TURN_PassList = 109,
    //k_ESteamNetworkingConfig_P2P_Transport_LANBeacon_Penalty = 107,
    k_ESteamNetworkingConfig_P2P_Transport_ICE_Implementation = 110,

//
// Settings for SDR relayed connections
//

    /// [global int32] If the first N pings to a port all fail, mark that port as unavailable for
    /// a while, and try a different one.  Some ISPs and routers may drop the first
    /// packet, so setting this to 1 may greatly disrupt communications.
    k_ESteamNetworkingConfig_SDRClient_ConsecutitivePingTimeoutsFailInitial = 19,

    /// [global int32] If N consecutive pings to a port fail, after having received successful
    /// communication, mark that port as unavailable for a while, and try a
    /// different one.
    k_ESteamNetworkingConfig_SDRClient_ConsecutitivePingTimeoutsFail = 20,

    /// [global int32] Minimum number of lifetime pings we need to send, before we think our estimate
    /// is solid.  The first ping to each cluster is very often delayed because of NAT,
    /// routers not having the best route, etc.  Until we've sent a sufficient number
    /// of pings, our estimate is often inaccurate.  Keep pinging until we get this
    /// many pings.
    k_ESteamNetworkingConfig_SDRClient_MinPingsBeforePingAccurate = 21,

    /// [global int32] Set all steam datagram traffic to originate from the same
    /// local port. By default, we open up a new UDP socket (on a different local
    /// port) for each relay.  This is slightly less optimal, but it works around
    /// some routers that don't implement NAT properly.  If you have intermittent
    /// problems talking to relays that might be NAT related, try toggling
    /// this flag
    k_ESteamNetworkingConfig_SDRClient_SingleSocket = 22,

    /// [global string] Code of relay cluster to force use.  If not empty, we will
    /// only use relays in that cluster.  E.g. 'iad'
    k_ESteamNetworkingConfig_SDRClient_ForceRelayCluster = 29,

    /// [connection string] For development, a base-64 encoded ticket generated
    /// using the cert tool.  This can be used to connect to a gameserver via SDR
    /// without a ticket generated using the game coordinator.  (You will still
    /// need a key that is trusted for your app, however.)
    ///
    /// This can also be passed using the SDR_DEVTICKET environment variable
    k_ESteamNetworkingConfig_SDRClient_DevTicket = 30,

    /// [global string] For debugging.  Override list of relays from the config with
    /// this set (maybe just one).  Comma-separated list.
    k_ESteamNetworkingConfig_SDRClient_ForceProxyAddr = 31,

    /// [global string] For debugging.  Force ping times to clusters to be the specified
    /// values.  A comma separated list of <cluster>=<ms> values.  E.g. "sto=32,iad=100"
    ///
    /// This is a dev configuration value, you probably should not let users modify it
    /// in production.
    k_ESteamNetworkingConfig_SDRClient_FakeClusterPing = 36,

    /// [global int32] When probing the SteamDatagram network, we limit exploration
    /// to the closest N POPs, based on our current best approximated ping to that POP.
    k_ESteamNetworkingConfig_SDRClient_LimitPingProbesToNearestN = 60,

//
// Log levels for debugging information of various subsystems.
// Higher numeric values will cause more stuff to be printed.
// See ISteamNetworkingUtils::SetDebugOutputFunction for more
// information
//
// The default for all values is k_ESteamNetworkingSocketsDebugOutputType_Warning.
//
    k_ESteamNetworkingConfig_LogLevel_AckRTT = 13, // [connection int32] RTT calculations for inline pings and replies
    k_ESteamNetworkingConfig_LogLevel_PacketDecode = 14, // [connection int32] log SNP packets send/recv
    k_ESteamNetworkingConfig_LogLevel_Message = 15, // [connection int32] log each message send/recv
    k_ESteamNetworkingConfig_LogLevel_PacketGaps = 16, // [connection int32] dropped packets
    k_ESteamNetworkingConfig_LogLevel_P2PRendezvous = 17, // [connection int32] P2P rendezvous messages
    k_ESteamNetworkingConfig_LogLevel_SDRRelayPings = 18, // [global int32] Ping relays

    // Experimental.  Set the ECN header field on all outbound UDP packets
    // -1 = the default, and means "don't set anything".
    // 0..3 = set that value.  (Even though 0 is the default UDP ECN value, a 0 here means "explicitly set a 0".)
    k_ESteamNetworkingConfig_ECN = 999,

    // Deleted, do not use
    k_ESteamNetworkingConfig_DELETED_EnumerateDevVars = 35,

    k_ESteamNetworkingConfigValue__Force32Bit = 0x7fffffff
} ESteamNetworkingConfigValue;

/// Return value of ISteamNetworkintgUtils::GetConfigValue
typedef enum ESteamNetworkingGetConfigValueResult
{
    k_ESteamNetworkingGetConfigValue_BadValue = -1,	// No such configuration value
    k_ESteamNetworkingGetConfigValue_BadScopeObj = -2,	// Bad connection handle, etc
    k_ESteamNetworkingGetConfigValue_BufferTooSmall = -3, // Couldn't fit the result in your buffer
    k_ESteamNetworkingGetConfigValue_OK = 1,
    k_ESteamNetworkingGetConfigValue_OKInherited = 2, // A value was not set at this level, but the effective (inherited) value was returned.

    k_ESteamNetworkingGetConfigValueResult__Force32Bit = 0x7fffffff
} ESteamNetworkingGetConfigValueResult;

typedef void (*FnSteamNetConnectionStatusChanged)( SteamNetConnectionStatusChangedCallback_t * );
typedef void (*FnSteamNetAuthenticationStatusChanged)( SteamNetAuthenticationStatus_t * );
typedef void (*FnSteamRelayNetworkStatusChanged)(SteamRelayNetworkStatus_t *);
typedef void (*FnSteamNetworkingMessagesSessionRequest)(SteamNetworkingMessagesSessionRequest_t *);
typedef void (*FnSteamNetworkingMessagesSessionFailed)(SteamNetworkingMessagesSessionFailed_t *);
typedef void (*FnSteamNetworkingFakeIPResult)(SteamNetworkingFakeIPResult_t *);

#pragma pack( pop )

// ISteamClient
S_API HSteamPipe SteamAPI_ISteamClient_CreateSteamPipe( ISteamClient* self );
S_API bool SteamAPI_ISteamClient_BReleaseSteamPipe( ISteamClient* self, HSteamPipe hSteamPipe );
S_API HSteamUser SteamAPI_ISteamClient_ConnectToGlobalUser( ISteamClient* self, HSteamPipe hSteamPipe );
S_API HSteamUser SteamAPI_ISteamClient_CreateLocalUser( ISteamClient* self, HSteamPipe * phSteamPipe, EAccountType eAccountType );
S_API void SteamAPI_ISteamClient_ReleaseUser( ISteamClient* self, HSteamPipe hSteamPipe, HSteamUser hUser );
S_API ISteamUser * SteamAPI_ISteamClient_GetISteamUser( ISteamClient* self, HSteamUser hSteamUser, HSteamPipe hSteamPipe, const char * pchVersion );
S_API ISteamGameServer * SteamAPI_ISteamClient_GetISteamGameServer( ISteamClient* self, HSteamUser hSteamUser, HSteamPipe hSteamPipe, const char * pchVersion );
S_API void SteamAPI_ISteamClient_SetLocalIPBinding( ISteamClient* self, const SteamIPAddress_t & unIP, uint16 usPort );
S_API ISteamFriends * SteamAPI_ISteamClient_GetISteamFriends( ISteamClient* self, HSteamUser hSteamUser, HSteamPipe hSteamPipe, const char * pchVersion );
S_API ISteamUtils * SteamAPI_ISteamClient_GetISteamUtils( ISteamClient* self, HSteamPipe hSteamPipe, const char * pchVersion );
S_API ISteamMatchmaking * SteamAPI_ISteamClient_GetISteamMatchmaking( ISteamClient* self, HSteamUser hSteamUser, HSteamPipe hSteamPipe, const char * pchVersion );
S_API ISteamMatchmakingServers * SteamAPI_ISteamClient_GetISteamMatchmakingServers( ISteamClient* self, HSteamUser hSteamUser, HSteamPipe hSteamPipe, const char * pchVersion );
S_API void * SteamAPI_ISteamClient_GetISteamGenericInterface( ISteamClient* self, HSteamUser hSteamUser, HSteamPipe hSteamPipe, const char * pchVersion );
S_API ISteamUserStats * SteamAPI_ISteamClient_GetISteamUserStats( ISteamClient* self, HSteamUser hSteamUser, HSteamPipe hSteamPipe, const char * pchVersion );
S_API ISteamGameServerStats * SteamAPI_ISteamClient_GetISteamGameServerStats( ISteamClient* self, HSteamUser hSteamuser, HSteamPipe hSteamPipe, const char * pchVersion );
S_API ISteamApps * SteamAPI_ISteamClient_GetISteamApps( ISteamClient* self, HSteamUser hSteamUser, HSteamPipe hSteamPipe, const char * pchVersion );
S_API ISteamNetworking * SteamAPI_ISteamClient_GetISteamNetworking( ISteamClient* self, HSteamUser hSteamUser, HSteamPipe hSteamPipe, const char * pchVersion );
S_API ISteamRemoteStorage * SteamAPI_ISteamClient_GetISteamRemoteStorage( ISteamClient* self, HSteamUser hSteamuser, HSteamPipe hSteamPipe, const char * pchVersion );
S_API ISteamScreenshots * SteamAPI_ISteamClient_GetISteamScreenshots( ISteamClient* self, HSteamUser hSteamuser, HSteamPipe hSteamPipe, const char * pchVersion );
S_API ISteamGameSearch * SteamAPI_ISteamClient_GetISteamGameSearch( ISteamClient* self, HSteamUser hSteamuser, HSteamPipe hSteamPipe, const char * pchVersion );
S_API uint32 SteamAPI_ISteamClient_GetIPCCallCount( ISteamClient* self );
S_API void SteamAPI_ISteamClient_SetWarningMessageHook( ISteamClient* self, SteamAPIWarningMessageHook_t pFunction );
S_API bool SteamAPI_ISteamClient_BShutdownIfAllPipesClosed( ISteamClient* self );
S_API ISteamHTTP * SteamAPI_ISteamClient_GetISteamHTTP( ISteamClient* self, HSteamUser hSteamuser, HSteamPipe hSteamPipe, const char * pchVersion );
S_API ISteamController * SteamAPI_ISteamClient_GetISteamController( ISteamClient* self, HSteamUser hSteamUser, HSteamPipe hSteamPipe, const char * pchVersion );
S_API ISteamUGC * SteamAPI_ISteamClient_GetISteamUGC( ISteamClient* self, HSteamUser hSteamUser, HSteamPipe hSteamPipe, const char * pchVersion );
S_API ISteamMusic * SteamAPI_ISteamClient_GetISteamMusic( ISteamClient* self, HSteamUser hSteamuser, HSteamPipe hSteamPipe, const char * pchVersion );
S_API ISteamMusicRemote * SteamAPI_ISteamClient_GetISteamMusicRemote( ISteamClient* self, HSteamUser hSteamuser, HSteamPipe hSteamPipe, const char * pchVersion );
S_API ISteamHTMLSurface * SteamAPI_ISteamClient_GetISteamHTMLSurface( ISteamClient* self, HSteamUser hSteamuser, HSteamPipe hSteamPipe, const char * pchVersion );
S_API ISteamInventory * SteamAPI_ISteamClient_GetISteamInventory( ISteamClient* self, HSteamUser hSteamuser, HSteamPipe hSteamPipe, const char * pchVersion );
S_API ISteamVideo * SteamAPI_ISteamClient_GetISteamVideo( ISteamClient* self, HSteamUser hSteamuser, HSteamPipe hSteamPipe, const char * pchVersion );
S_API ISteamParentalSettings * SteamAPI_ISteamClient_GetISteamParentalSettings( ISteamClient* self, HSteamUser hSteamuser, HSteamPipe hSteamPipe, const char * pchVersion );
S_API ISteamInput * SteamAPI_ISteamClient_GetISteamInput( ISteamClient* self, HSteamUser hSteamUser, HSteamPipe hSteamPipe, const char * pchVersion );
S_API ISteamParties * SteamAPI_ISteamClient_GetISteamParties( ISteamClient* self, HSteamUser hSteamUser, HSteamPipe hSteamPipe, const char * pchVersion );
S_API ISteamRemotePlay * SteamAPI_ISteamClient_GetISteamRemotePlay( ISteamClient* self, HSteamUser hSteamUser, HSteamPipe hSteamPipe, const char * pchVersion );

// ISteamUser

// A versioned accessor is exported by the library
S_API ISteamUser *SteamAPI_SteamUser_v023();
// Inline, unversioned accessor to get the current version.  Essentially the same as SteamUser(), but using this ensures that you are using a matching library.
inline ISteamUser *SteamAPI_SteamUser() { return SteamAPI_SteamUser_v023(); }
S_API HSteamUser SteamAPI_ISteamUser_GetHSteamUser( ISteamUser* self );
S_API bool SteamAPI_ISteamUser_BLoggedOn( ISteamUser* self );
S_API uint64_steamid SteamAPI_ISteamUser_GetSteamID( ISteamUser* self );
S_API int SteamAPI_ISteamUser_InitiateGameConnection_DEPRECATED( ISteamUser* self, void * pAuthBlob, int cbMaxAuthBlob, uint64_steamid steamIDGameServer, uint32 unIPServer, uint16 usPortServer, bool bSecure );
S_API void SteamAPI_ISteamUser_TerminateGameConnection_DEPRECATED( ISteamUser* self, uint32 unIPServer, uint16 usPortServer );
S_API void SteamAPI_ISteamUser_TrackAppUsageEvent( ISteamUser* self, uint64_gameid gameID, int eAppUsageEvent, const char * pchExtraInfo );
S_API bool SteamAPI_ISteamUser_GetUserDataFolder( ISteamUser* self, char * pchBuffer, int cubBuffer );
S_API void SteamAPI_ISteamUser_StartVoiceRecording( ISteamUser* self );
S_API void SteamAPI_ISteamUser_StopVoiceRecording( ISteamUser* self );
S_API EVoiceResult SteamAPI_ISteamUser_GetAvailableVoice( ISteamUser* self, uint32 * pcbCompressed, uint32 * pcbUncompressed_Deprecated, uint32 nUncompressedVoiceDesiredSampleRate_Deprecated );
S_API EVoiceResult SteamAPI_ISteamUser_GetVoice( ISteamUser* self, bool bWantCompressed, void * pDestBuffer, uint32 cbDestBufferSize, uint32 * nBytesWritten, bool bWantUncompressed_Deprecated, void * pUncompressedDestBuffer_Deprecated, uint32 cbUncompressedDestBufferSize_Deprecated, uint32 * nUncompressBytesWritten_Deprecated, uint32 nUncompressedVoiceDesiredSampleRate_Deprecated );
S_API EVoiceResult SteamAPI_ISteamUser_DecompressVoice( ISteamUser* self, const void * pCompressed, uint32 cbCompressed, void * pDestBuffer, uint32 cbDestBufferSize, uint32 * nBytesWritten, uint32 nDesiredSampleRate );
S_API uint32 SteamAPI_ISteamUser_GetVoiceOptimalSampleRate( ISteamUser* self );
S_API HAuthTicket SteamAPI_ISteamUser_GetAuthSessionTicket( ISteamUser* self, void * pTicket, int cbMaxTicket, uint32 * pcbTicket, const SteamNetworkingIdentity * pSteamNetworkingIdentity );
S_API HAuthTicket SteamAPI_ISteamUser_GetAuthTicketForWebApi( ISteamUser* self, const char * pchIdentity );
S_API EBeginAuthSessionResult SteamAPI_ISteamUser_BeginAuthSession( ISteamUser* self, const void * pAuthTicket, int cbAuthTicket, uint64_steamid steamID );
S_API void SteamAPI_ISteamUser_EndAuthSession( ISteamUser* self, uint64_steamid steamID );
S_API void SteamAPI_ISteamUser_CancelAuthTicket( ISteamUser* self, HAuthTicket hAuthTicket );
S_API EUserHasLicenseForAppResult SteamAPI_ISteamUser_UserHasLicenseForApp( ISteamUser* self, uint64_steamid steamID, AppId_t appID );
S_API bool SteamAPI_ISteamUser_BIsBehindNAT( ISteamUser* self );
S_API void SteamAPI_ISteamUser_AdvertiseGame( ISteamUser* self, uint64_steamid steamIDGameServer, uint32 unIPServer, uint16 usPortServer );
S_API SteamAPICall_t SteamAPI_ISteamUser_RequestEncryptedAppTicket( ISteamUser* self, void * pDataToInclude, int cbDataToInclude );
S_API bool SteamAPI_ISteamUser_GetEncryptedAppTicket( ISteamUser* self, void * pTicket, int cbMaxTicket, uint32 * pcbTicket );
S_API int SteamAPI_ISteamUser_GetGameBadgeLevel( ISteamUser* self, int nSeries, bool bFoil );
S_API int SteamAPI_ISteamUser_GetPlayerSteamLevel( ISteamUser* self );
S_API SteamAPICall_t SteamAPI_ISteamUser_RequestStoreAuthURL( ISteamUser* self, const char * pchRedirectURL );
S_API bool SteamAPI_ISteamUser_BIsPhoneVerified( ISteamUser* self );
S_API bool SteamAPI_ISteamUser_BIsTwoFactorEnabled( ISteamUser* self );
S_API bool SteamAPI_ISteamUser_BIsPhoneIdentifying( ISteamUser* self );
S_API bool SteamAPI_ISteamUser_BIsPhoneRequiringVerification( ISteamUser* self );
S_API SteamAPICall_t SteamAPI_ISteamUser_GetMarketEligibility( ISteamUser* self );
S_API SteamAPICall_t SteamAPI_ISteamUser_GetDurationControl( ISteamUser* self );
S_API bool SteamAPI_ISteamUser_BSetDurationControlOnlineState( ISteamUser* self, EDurationControlOnlineState eNewState );

// ISteamFriends

// A versioned accessor is exported by the library
S_API ISteamFriends *SteamAPI_SteamFriends_v017();
// Inline, unversioned accessor to get the current version.  Essentially the same as SteamFriends(), but using this ensures that you are using a matching library.
inline ISteamFriends *SteamAPI_SteamFriends() { return SteamAPI_SteamFriends_v017(); }
S_API const char * SteamAPI_ISteamFriends_GetPersonaName( ISteamFriends* self );
S_API SteamAPICall_t SteamAPI_ISteamFriends_SetPersonaName( ISteamFriends* self, const char * pchPersonaName );
S_API EPersonaState SteamAPI_ISteamFriends_GetPersonaState( ISteamFriends* self );
S_API int SteamAPI_ISteamFriends_GetFriendCount( ISteamFriends* self, int iFriendFlags );
S_API uint64_steamid SteamAPI_ISteamFriends_GetFriendByIndex( ISteamFriends* self, int iFriend, int iFriendFlags );
S_API EFriendRelationship SteamAPI_ISteamFriends_GetFriendRelationship( ISteamFriends* self, uint64_steamid steamIDFriend );
S_API EPersonaState SteamAPI_ISteamFriends_GetFriendPersonaState( ISteamFriends* self, uint64_steamid steamIDFriend );
S_API const char * SteamAPI_ISteamFriends_GetFriendPersonaName( ISteamFriends* self, uint64_steamid steamIDFriend );
S_API bool SteamAPI_ISteamFriends_GetFriendGamePlayed( ISteamFriends* self, uint64_steamid steamIDFriend, FriendGameInfo_t * pFriendGameInfo );
S_API const char * SteamAPI_ISteamFriends_GetFriendPersonaNameHistory( ISteamFriends* self, uint64_steamid steamIDFriend, int iPersonaName );
S_API int SteamAPI_ISteamFriends_GetFriendSteamLevel( ISteamFriends* self, uint64_steamid steamIDFriend );
S_API const char * SteamAPI_ISteamFriends_GetPlayerNickname( ISteamFriends* self, uint64_steamid steamIDPlayer );
S_API int SteamAPI_ISteamFriends_GetFriendsGroupCount( ISteamFriends* self );
S_API FriendsGroupID_t SteamAPI_ISteamFriends_GetFriendsGroupIDByIndex( ISteamFriends* self, int iFG );
S_API const char * SteamAPI_ISteamFriends_GetFriendsGroupName( ISteamFriends* self, FriendsGroupID_t friendsGroupID );
S_API int SteamAPI_ISteamFriends_GetFriendsGroupMembersCount( ISteamFriends* self, FriendsGroupID_t friendsGroupID );
S_API void SteamAPI_ISteamFriends_GetFriendsGroupMembersList( ISteamFriends* self, FriendsGroupID_t friendsGroupID, CSteamID * pOutSteamIDMembers, int nMembersCount );
S_API bool SteamAPI_ISteamFriends_HasFriend( ISteamFriends* self, uint64_steamid steamIDFriend, int iFriendFlags );
S_API int SteamAPI_ISteamFriends_GetClanCount( ISteamFriends* self );
S_API uint64_steamid SteamAPI_ISteamFriends_GetClanByIndex( ISteamFriends* self, int iClan );
S_API const char * SteamAPI_ISteamFriends_GetClanName( ISteamFriends* self, uint64_steamid steamIDClan );
S_API const char * SteamAPI_ISteamFriends_GetClanTag( ISteamFriends* self, uint64_steamid steamIDClan );
S_API bool SteamAPI_ISteamFriends_GetClanActivityCounts( ISteamFriends* self, uint64_steamid steamIDClan, int * pnOnline, int * pnInGame, int * pnChatting );
S_API SteamAPICall_t SteamAPI_ISteamFriends_DownloadClanActivityCounts( ISteamFriends* self, CSteamID * psteamIDClans, int cClansToRequest );
S_API int SteamAPI_ISteamFriends_GetFriendCountFromSource( ISteamFriends* self, uint64_steamid steamIDSource );
S_API uint64_steamid SteamAPI_ISteamFriends_GetFriendFromSourceByIndex( ISteamFriends* self, uint64_steamid steamIDSource, int iFriend );
S_API bool SteamAPI_ISteamFriends_IsUserInSource( ISteamFriends* self, uint64_steamid steamIDUser, uint64_steamid steamIDSource );
S_API void SteamAPI_ISteamFriends_SetInGameVoiceSpeaking( ISteamFriends* self, uint64_steamid steamIDUser, bool bSpeaking );
S_API void SteamAPI_ISteamFriends_ActivateGameOverlay( ISteamFriends* self, const char * pchDialog );
S_API void SteamAPI_ISteamFriends_ActivateGameOverlayToUser( ISteamFriends* self, const char * pchDialog, uint64_steamid steamID );
S_API void SteamAPI_ISteamFriends_ActivateGameOverlayToWebPage( ISteamFriends* self, const char * pchURL, EActivateGameOverlayToWebPageMode eMode );
S_API void SteamAPI_ISteamFriends_ActivateGameOverlayToStore( ISteamFriends* self, AppId_t nAppID, EOverlayToStoreFlag eFlag );
S_API void SteamAPI_ISteamFriends_SetPlayedWith( ISteamFriends* self, uint64_steamid steamIDUserPlayedWith );
S_API void SteamAPI_ISteamFriends_ActivateGameOverlayInviteDialog( ISteamFriends* self, uint64_steamid steamIDLobby );
S_API int SteamAPI_ISteamFriends_GetSmallFriendAvatar( ISteamFriends* self, uint64_steamid steamIDFriend );
S_API int SteamAPI_ISteamFriends_GetMediumFriendAvatar( ISteamFriends* self, uint64_steamid steamIDFriend );
S_API int SteamAPI_ISteamFriends_GetLargeFriendAvatar( ISteamFriends* self, uint64_steamid steamIDFriend );
S_API bool SteamAPI_ISteamFriends_RequestUserInformation( ISteamFriends* self, uint64_steamid steamIDUser, bool bRequireNameOnly );
S_API SteamAPICall_t SteamAPI_ISteamFriends_RequestClanOfficerList( ISteamFriends* self, uint64_steamid steamIDClan );
S_API uint64_steamid SteamAPI_ISteamFriends_GetClanOwner( ISteamFriends* self, uint64_steamid steamIDClan );
S_API int SteamAPI_ISteamFriends_GetClanOfficerCount( ISteamFriends* self, uint64_steamid steamIDClan );
S_API uint64_steamid SteamAPI_ISteamFriends_GetClanOfficerByIndex( ISteamFriends* self, uint64_steamid steamIDClan, int iOfficer );
S_API uint32 SteamAPI_ISteamFriends_GetUserRestrictions( ISteamFriends* self );
S_API bool SteamAPI_ISteamFriends_SetRichPresence( ISteamFriends* self, const char * pchKey, const char * pchValue );
S_API void SteamAPI_ISteamFriends_ClearRichPresence( ISteamFriends* self );
S_API const char * SteamAPI_ISteamFriends_GetFriendRichPresence( ISteamFriends* self, uint64_steamid steamIDFriend, const char * pchKey );
S_API int SteamAPI_ISteamFriends_GetFriendRichPresenceKeyCount( ISteamFriends* self, uint64_steamid steamIDFriend );
S_API const char * SteamAPI_ISteamFriends_GetFriendRichPresenceKeyByIndex( ISteamFriends* self, uint64_steamid steamIDFriend, int iKey );
S_API void SteamAPI_ISteamFriends_RequestFriendRichPresence( ISteamFriends* self, uint64_steamid steamIDFriend );
S_API bool SteamAPI_ISteamFriends_InviteUserToGame( ISteamFriends* self, uint64_steamid steamIDFriend, const char * pchConnectString );
S_API int SteamAPI_ISteamFriends_GetCoplayFriendCount( ISteamFriends* self );
S_API uint64_steamid SteamAPI_ISteamFriends_GetCoplayFriend( ISteamFriends* self, int iCoplayFriend );
S_API int SteamAPI_ISteamFriends_GetFriendCoplayTime( ISteamFriends* self, uint64_steamid steamIDFriend );
S_API AppId_t SteamAPI_ISteamFriends_GetFriendCoplayGame( ISteamFriends* self, uint64_steamid steamIDFriend );
S_API SteamAPICall_t SteamAPI_ISteamFriends_JoinClanChatRoom( ISteamFriends* self, uint64_steamid steamIDClan );
S_API bool SteamAPI_ISteamFriends_LeaveClanChatRoom( ISteamFriends* self, uint64_steamid steamIDClan );
S_API int SteamAPI_ISteamFriends_GetClanChatMemberCount( ISteamFriends* self, uint64_steamid steamIDClan );
S_API uint64_steamid SteamAPI_ISteamFriends_GetChatMemberByIndex( ISteamFriends* self, uint64_steamid steamIDClan, int iUser );
S_API bool SteamAPI_ISteamFriends_SendClanChatMessage( ISteamFriends* self, uint64_steamid steamIDClanChat, const char * pchText );
S_API int SteamAPI_ISteamFriends_GetClanChatMessage( ISteamFriends* self, uint64_steamid steamIDClanChat, int iMessage, void * prgchText, int cchTextMax, EChatEntryType * peChatEntryType, CSteamID * psteamidChatter );
S_API bool SteamAPI_ISteamFriends_IsClanChatAdmin( ISteamFriends* self, uint64_steamid steamIDClanChat, uint64_steamid steamIDUser );
S_API bool SteamAPI_ISteamFriends_IsClanChatWindowOpenInSteam( ISteamFriends* self, uint64_steamid steamIDClanChat );
S_API bool SteamAPI_ISteamFriends_OpenClanChatWindowInSteam( ISteamFriends* self, uint64_steamid steamIDClanChat );
S_API bool SteamAPI_ISteamFriends_CloseClanChatWindowInSteam( ISteamFriends* self, uint64_steamid steamIDClanChat );
S_API bool SteamAPI_ISteamFriends_SetListenForFriendsMessages( ISteamFriends* self, bool bInterceptEnabled );
S_API bool SteamAPI_ISteamFriends_ReplyToFriendMessage( ISteamFriends* self, uint64_steamid steamIDFriend, const char * pchMsgToSend );
S_API int SteamAPI_ISteamFriends_GetFriendMessage( ISteamFriends* self, uint64_steamid steamIDFriend, int iMessageID, void * pvData, int cubData, EChatEntryType * peChatEntryType );
S_API SteamAPICall_t SteamAPI_ISteamFriends_GetFollowerCount( ISteamFriends* self, uint64_steamid steamID );
S_API SteamAPICall_t SteamAPI_ISteamFriends_IsFollowing( ISteamFriends* self, uint64_steamid steamID );
S_API SteamAPICall_t SteamAPI_ISteamFriends_EnumerateFollowingList( ISteamFriends* self, uint32 unStartIndex );
S_API bool SteamAPI_ISteamFriends_IsClanPublic( ISteamFriends* self, uint64_steamid steamIDClan );
S_API bool SteamAPI_ISteamFriends_IsClanOfficialGameGroup( ISteamFriends* self, uint64_steamid steamIDClan );
S_API int SteamAPI_ISteamFriends_GetNumChatsWithUnreadPriorityMessages( ISteamFriends* self );
S_API void SteamAPI_ISteamFriends_ActivateGameOverlayRemotePlayTogetherInviteDialog( ISteamFriends* self, uint64_steamid steamIDLobby );
S_API bool SteamAPI_ISteamFriends_RegisterProtocolInOverlayBrowser( ISteamFriends* self, const char * pchProtocol );
S_API void SteamAPI_ISteamFriends_ActivateGameOverlayInviteDialogConnectString( ISteamFriends* self, const char * pchConnectString );
S_API SteamAPICall_t SteamAPI_ISteamFriends_RequestEquippedProfileItems( ISteamFriends* self, uint64_steamid steamID );
S_API bool SteamAPI_ISteamFriends_BHasEquippedProfileItem( ISteamFriends* self, uint64_steamid steamID, ECommunityProfileItemType itemType );
S_API const char * SteamAPI_ISteamFriends_GetProfileItemPropertyString( ISteamFriends* self, uint64_steamid steamID, ECommunityProfileItemType itemType, ECommunityProfileItemProperty prop );
S_API uint32 SteamAPI_ISteamFriends_GetProfileItemPropertyUint( ISteamFriends* self, uint64_steamid steamID, ECommunityProfileItemType itemType, ECommunityProfileItemProperty prop );

// ISteamUtils

// A versioned accessor is exported by the library
S_API ISteamUtils *SteamAPI_SteamUtils_v010();
// Inline, unversioned accessor to get the current version.  Essentially the same as SteamUtils(), but using this ensures that you are using a matching library.
inline ISteamUtils *SteamAPI_SteamUtils() { return SteamAPI_SteamUtils_v010(); }

// A versioned accessor is exported by the library
S_API ISteamUtils *SteamAPI_SteamGameServerUtils_v010();
// Inline, unversioned accessor to get the current version.  Essentially the same as SteamGameServerUtils(), but using this ensures that you are using a matching library.
inline ISteamUtils *SteamAPI_SteamGameServerUtils() { return SteamAPI_SteamGameServerUtils_v010(); }
S_API uint32 SteamAPI_ISteamUtils_GetSecondsSinceAppActive( ISteamUtils* self );
S_API uint32 SteamAPI_ISteamUtils_GetSecondsSinceComputerActive( ISteamUtils* self );
S_API EUniverse SteamAPI_ISteamUtils_GetConnectedUniverse( ISteamUtils* self );
S_API uint32 SteamAPI_ISteamUtils_GetServerRealTime( ISteamUtils* self );
S_API const char * SteamAPI_ISteamUtils_GetIPCountry( ISteamUtils* self );
S_API bool SteamAPI_ISteamUtils_GetImageSize( ISteamUtils* self, int iImage, uint32 * pnWidth, uint32 * pnHeight );
S_API bool SteamAPI_ISteamUtils_GetImageRGBA( ISteamUtils* self, int iImage, uint8 * pubDest, int nDestBufferSize );
S_API uint8 SteamAPI_ISteamUtils_GetCurrentBatteryPower( ISteamUtils* self );
S_API uint32 SteamAPI_ISteamUtils_GetAppID( ISteamUtils* self );
S_API void SteamAPI_ISteamUtils_SetOverlayNotificationPosition( ISteamUtils* self, ENotificationPosition eNotificationPosition );
S_API bool SteamAPI_ISteamUtils_IsAPICallCompleted( ISteamUtils* self, SteamAPICall_t hSteamAPICall, bool * pbFailed );
S_API ESteamAPICallFailure SteamAPI_ISteamUtils_GetAPICallFailureReason( ISteamUtils* self, SteamAPICall_t hSteamAPICall );
S_API bool SteamAPI_ISteamUtils_GetAPICallResult( ISteamUtils* self, SteamAPICall_t hSteamAPICall, void * pCallback, int cubCallback, int iCallbackExpected, bool * pbFailed );
S_API uint32 SteamAPI_ISteamUtils_GetIPCCallCount( ISteamUtils* self );
S_API void SteamAPI_ISteamUtils_SetWarningMessageHook( ISteamUtils* self, SteamAPIWarningMessageHook_t pFunction );
S_API bool SteamAPI_ISteamUtils_IsOverlayEnabled( ISteamUtils* self );
S_API bool SteamAPI_ISteamUtils_BOverlayNeedsPresent( ISteamUtils* self );
S_API SteamAPICall_t SteamAPI_ISteamUtils_CheckFileSignature( ISteamUtils* self, const char * szFileName );
S_API bool SteamAPI_ISteamUtils_ShowGamepadTextInput( ISteamUtils* self, EGamepadTextInputMode eInputMode, EGamepadTextInputLineMode eLineInputMode, const char * pchDescription, uint32 unCharMax, const char * pchExistingText );
S_API uint32 SteamAPI_ISteamUtils_GetEnteredGamepadTextLength( ISteamUtils* self );
S_API bool SteamAPI_ISteamUtils_GetEnteredGamepadTextInput( ISteamUtils* self, char * pchText, uint32 cchText );
S_API const char * SteamAPI_ISteamUtils_GetSteamUILanguage( ISteamUtils* self );
S_API bool SteamAPI_ISteamUtils_IsSteamRunningInVR( ISteamUtils* self );
S_API void SteamAPI_ISteamUtils_SetOverlayNotificationInset( ISteamUtils* self, int nHorizontalInset, int nVerticalInset );
S_API bool SteamAPI_ISteamUtils_IsSteamInBigPictureMode( ISteamUtils* self );
S_API void SteamAPI_ISteamUtils_StartVRDashboard( ISteamUtils* self );
S_API bool SteamAPI_ISteamUtils_IsVRHeadsetStreamingEnabled( ISteamUtils* self );
S_API void SteamAPI_ISteamUtils_SetVRHeadsetStreamingEnabled( ISteamUtils* self, bool bEnabled );
S_API bool SteamAPI_ISteamUtils_IsSteamChinaLauncher( ISteamUtils* self );
S_API bool SteamAPI_ISteamUtils_InitFilterText( ISteamUtils* self, uint32 unFilterOptions );
S_API int SteamAPI_ISteamUtils_FilterText( ISteamUtils* self, ETextFilteringContext eContext, uint64_steamid sourceSteamID, const char * pchInputMessage, char * pchOutFilteredText, uint32 nByteSizeOutFilteredText );
S_API ESteamIPv6ConnectivityState SteamAPI_ISteamUtils_GetIPv6ConnectivityState( ISteamUtils* self, ESteamIPv6ConnectivityProtocol eProtocol );
S_API bool SteamAPI_ISteamUtils_IsSteamRunningOnSteamDeck( ISteamUtils* self );
S_API bool SteamAPI_ISteamUtils_ShowFloatingGamepadTextInput( ISteamUtils* self, EFloatingGamepadTextInputMode eKeyboardMode, int nTextFieldXPosition, int nTextFieldYPosition, int nTextFieldWidth, int nTextFieldHeight );
S_API void SteamAPI_ISteamUtils_SetGameLauncherMode( ISteamUtils* self, bool bLauncherMode );
S_API bool SteamAPI_ISteamUtils_DismissFloatingGamepadTextInput( ISteamUtils* self );
S_API bool SteamAPI_ISteamUtils_DismissGamepadTextInput( ISteamUtils* self );

// ISteamMatchmaking

// A versioned accessor is exported by the library
S_API ISteamMatchmaking *SteamAPI_SteamMatchmaking_v009();
// Inline, unversioned accessor to get the current version.  Essentially the same as SteamMatchmaking(), but using this ensures that you are using a matching library.
inline ISteamMatchmaking *SteamAPI_SteamMatchmaking() { return SteamAPI_SteamMatchmaking_v009(); }
S_API int SteamAPI_ISteamMatchmaking_GetFavoriteGameCount( ISteamMatchmaking* self );
S_API bool SteamAPI_ISteamMatchmaking_GetFavoriteGame( ISteamMatchmaking* self, int iGame, AppId_t * pnAppID, uint32 * pnIP, uint16 * pnConnPort, uint16 * pnQueryPort, uint32 * punFlags, uint32 * pRTime32LastPlayedOnServer );
S_API int SteamAPI_ISteamMatchmaking_AddFavoriteGame( ISteamMatchmaking* self, AppId_t nAppID, uint32 nIP, uint16 nConnPort, uint16 nQueryPort, uint32 unFlags, uint32 rTime32LastPlayedOnServer );
S_API bool SteamAPI_ISteamMatchmaking_RemoveFavoriteGame( ISteamMatchmaking* self, AppId_t nAppID, uint32 nIP, uint16 nConnPort, uint16 nQueryPort, uint32 unFlags );
S_API SteamAPICall_t SteamAPI_ISteamMatchmaking_RequestLobbyList( ISteamMatchmaking* self );
S_API void SteamAPI_ISteamMatchmaking_AddRequestLobbyListStringFilter( ISteamMatchmaking* self, const char * pchKeyToMatch, const char * pchValueToMatch, ELobbyComparison eComparisonType );
S_API void SteamAPI_ISteamMatchmaking_AddRequestLobbyListNumericalFilter( ISteamMatchmaking* self, const char * pchKeyToMatch, int nValueToMatch, ELobbyComparison eComparisonType );
S_API void SteamAPI_ISteamMatchmaking_AddRequestLobbyListNearValueFilter( ISteamMatchmaking* self, const char * pchKeyToMatch, int nValueToBeCloseTo );
S_API void SteamAPI_ISteamMatchmaking_AddRequestLobbyListFilterSlotsAvailable( ISteamMatchmaking* self, int nSlotsAvailable );
S_API void SteamAPI_ISteamMatchmaking_AddRequestLobbyListDistanceFilter( ISteamMatchmaking* self, ELobbyDistanceFilter eLobbyDistanceFilter );
S_API void SteamAPI_ISteamMatchmaking_AddRequestLobbyListResultCountFilter( ISteamMatchmaking* self, int cMaxResults );
S_API void SteamAPI_ISteamMatchmaking_AddRequestLobbyListCompatibleMembersFilter( ISteamMatchmaking* self, uint64_steamid steamIDLobby );
S_API uint64_steamid SteamAPI_ISteamMatchmaking_GetLobbyByIndex( ISteamMatchmaking* self, int iLobby );
S_API SteamAPICall_t SteamAPI_ISteamMatchmaking_CreateLobby( ISteamMatchmaking* self, ELobbyType eLobbyType, int cMaxMembers );
S_API SteamAPICall_t SteamAPI_ISteamMatchmaking_JoinLobby( ISteamMatchmaking* self, uint64_steamid steamIDLobby );
S_API void SteamAPI_ISteamMatchmaking_LeaveLobby( ISteamMatchmaking* self, uint64_steamid steamIDLobby );
S_API bool SteamAPI_ISteamMatchmaking_InviteUserToLobby( ISteamMatchmaking* self, uint64_steamid steamIDLobby, uint64_steamid steamIDInvitee );
S_API int SteamAPI_ISteamMatchmaking_GetNumLobbyMembers( ISteamMatchmaking* self, uint64_steamid steamIDLobby );
S_API uint64_steamid SteamAPI_ISteamMatchmaking_GetLobbyMemberByIndex( ISteamMatchmaking* self, uint64_steamid steamIDLobby, int iMember );
S_API const char * SteamAPI_ISteamMatchmaking_GetLobbyData( ISteamMatchmaking* self, uint64_steamid steamIDLobby, const char * pchKey );
S_API bool SteamAPI_ISteamMatchmaking_SetLobbyData( ISteamMatchmaking* self, uint64_steamid steamIDLobby, const char * pchKey, const char * pchValue );
S_API int SteamAPI_ISteamMatchmaking_GetLobbyDataCount( ISteamMatchmaking* self, uint64_steamid steamIDLobby );
S_API bool SteamAPI_ISteamMatchmaking_GetLobbyDataByIndex( ISteamMatchmaking* self, uint64_steamid steamIDLobby, int iLobbyData, char * pchKey, int cchKeyBufferSize, char * pchValue, int cchValueBufferSize );
S_API bool SteamAPI_ISteamMatchmaking_DeleteLobbyData( ISteamMatchmaking* self, uint64_steamid steamIDLobby, const char * pchKey );
S_API const char * SteamAPI_ISteamMatchmaking_GetLobbyMemberData( ISteamMatchmaking* self, uint64_steamid steamIDLobby, uint64_steamid steamIDUser, const char * pchKey );
S_API void SteamAPI_ISteamMatchmaking_SetLobbyMemberData( ISteamMatchmaking* self, uint64_steamid steamIDLobby, const char * pchKey, const char * pchValue );
S_API bool SteamAPI_ISteamMatchmaking_SendLobbyChatMsg( ISteamMatchmaking* self, uint64_steamid steamIDLobby, const void * pvMsgBody, int cubMsgBody );
S_API int SteamAPI_ISteamMatchmaking_GetLobbyChatEntry( ISteamMatchmaking* self, uint64_steamid steamIDLobby, int iChatID, CSteamID * pSteamIDUser, void * pvData, int cubData, EChatEntryType * peChatEntryType );
S_API bool SteamAPI_ISteamMatchmaking_RequestLobbyData( ISteamMatchmaking* self, uint64_steamid steamIDLobby );
S_API void SteamAPI_ISteamMatchmaking_SetLobbyGameServer( ISteamMatchmaking* self, uint64_steamid steamIDLobby, uint32 unGameServerIP, uint16 unGameServerPort, uint64_steamid steamIDGameServer );
S_API bool SteamAPI_ISteamMatchmaking_GetLobbyGameServer( ISteamMatchmaking* self, uint64_steamid steamIDLobby, uint32 * punGameServerIP, uint16 * punGameServerPort, CSteamID * psteamIDGameServer );
S_API bool SteamAPI_ISteamMatchmaking_SetLobbyMemberLimit( ISteamMatchmaking* self, uint64_steamid steamIDLobby, int cMaxMembers );
S_API int SteamAPI_ISteamMatchmaking_GetLobbyMemberLimit( ISteamMatchmaking* self, uint64_steamid steamIDLobby );
S_API bool SteamAPI_ISteamMatchmaking_SetLobbyType( ISteamMatchmaking* self, uint64_steamid steamIDLobby, ELobbyType eLobbyType );
S_API bool SteamAPI_ISteamMatchmaking_SetLobbyJoinable( ISteamMatchmaking* self, uint64_steamid steamIDLobby, bool bLobbyJoinable );
S_API uint64_steamid SteamAPI_ISteamMatchmaking_GetLobbyOwner( ISteamMatchmaking* self, uint64_steamid steamIDLobby );
S_API bool SteamAPI_ISteamMatchmaking_SetLobbyOwner( ISteamMatchmaking* self, uint64_steamid steamIDLobby, uint64_steamid steamIDNewOwner );
S_API bool SteamAPI_ISteamMatchmaking_SetLinkedLobby( ISteamMatchmaking* self, uint64_steamid steamIDLobby, uint64_steamid steamIDLobbyDependent );

// ISteamMatchmakingServerListResponse
S_API void SteamAPI_ISteamMatchmakingServerListResponse_ServerResponded( ISteamMatchmakingServerListResponse* self, HServerListRequest hRequest, int iServer );
S_API void SteamAPI_ISteamMatchmakingServerListResponse_ServerFailedToRespond( ISteamMatchmakingServerListResponse* self, HServerListRequest hRequest, int iServer );
S_API void SteamAPI_ISteamMatchmakingServerListResponse_RefreshComplete( ISteamMatchmakingServerListResponse* self, HServerListRequest hRequest, EMatchMakingServerResponse response );

// ISteamMatchmakingPingResponse
S_API void SteamAPI_ISteamMatchmakingPingResponse_ServerResponded( ISteamMatchmakingPingResponse* self, gameserveritem_t & server );
S_API void SteamAPI_ISteamMatchmakingPingResponse_ServerFailedToRespond( ISteamMatchmakingPingResponse* self );

// ISteamMatchmakingPlayersResponse
S_API void SteamAPI_ISteamMatchmakingPlayersResponse_AddPlayerToList( ISteamMatchmakingPlayersResponse* self, const char * pchName, int nScore, float flTimePlayed );
S_API void SteamAPI_ISteamMatchmakingPlayersResponse_PlayersFailedToRespond( ISteamMatchmakingPlayersResponse* self );
S_API void SteamAPI_ISteamMatchmakingPlayersResponse_PlayersRefreshComplete( ISteamMatchmakingPlayersResponse* self );

// ISteamMatchmakingRulesResponse
S_API void SteamAPI_ISteamMatchmakingRulesResponse_RulesResponded( ISteamMatchmakingRulesResponse* self, const char * pchRule, const char * pchValue );
S_API void SteamAPI_ISteamMatchmakingRulesResponse_RulesFailedToRespond( ISteamMatchmakingRulesResponse* self );
S_API void SteamAPI_ISteamMatchmakingRulesResponse_RulesRefreshComplete( ISteamMatchmakingRulesResponse* self );

// ISteamMatchmakingServers

// A versioned accessor is exported by the library
S_API ISteamMatchmakingServers *SteamAPI_SteamMatchmakingServers_v002();
// Inline, unversioned accessor to get the current version.  Essentially the same as SteamMatchmakingServers(), but using this ensures that you are using a matching library.
inline ISteamMatchmakingServers *SteamAPI_SteamMatchmakingServers() { return SteamAPI_SteamMatchmakingServers_v002(); }
S_API HServerListRequest SteamAPI_ISteamMatchmakingServers_RequestInternetServerList( ISteamMatchmakingServers* self, AppId_t iApp, MatchMakingKeyValuePair_t ** ppchFilters, uint32 nFilters, ISteamMatchmakingServerListResponse * pRequestServersResponse );
S_API HServerListRequest SteamAPI_ISteamMatchmakingServers_RequestLANServerList( ISteamMatchmakingServers* self, AppId_t iApp, ISteamMatchmakingServerListResponse * pRequestServersResponse );
S_API HServerListRequest SteamAPI_ISteamMatchmakingServers_RequestFriendsServerList( ISteamMatchmakingServers* self, AppId_t iApp, MatchMakingKeyValuePair_t ** ppchFilters, uint32 nFilters, ISteamMatchmakingServerListResponse * pRequestServersResponse );
S_API HServerListRequest SteamAPI_ISteamMatchmakingServers_RequestFavoritesServerList( ISteamMatchmakingServers* self, AppId_t iApp, MatchMakingKeyValuePair_t ** ppchFilters, uint32 nFilters, ISteamMatchmakingServerListResponse * pRequestServersResponse );
S_API HServerListRequest SteamAPI_ISteamMatchmakingServers_RequestHistoryServerList( ISteamMatchmakingServers* self, AppId_t iApp, MatchMakingKeyValuePair_t ** ppchFilters, uint32 nFilters, ISteamMatchmakingServerListResponse * pRequestServersResponse );
S_API HServerListRequest SteamAPI_ISteamMatchmakingServers_RequestSpectatorServerList( ISteamMatchmakingServers* self, AppId_t iApp, MatchMakingKeyValuePair_t ** ppchFilters, uint32 nFilters, ISteamMatchmakingServerListResponse * pRequestServersResponse );
S_API void SteamAPI_ISteamMatchmakingServers_ReleaseRequest( ISteamMatchmakingServers* self, HServerListRequest hServerListRequest );
S_API gameserveritem_t * SteamAPI_ISteamMatchmakingServers_GetServerDetails( ISteamMatchmakingServers* self, HServerListRequest hRequest, int iServer );
S_API void SteamAPI_ISteamMatchmakingServers_CancelQuery( ISteamMatchmakingServers* self, HServerListRequest hRequest );
S_API void SteamAPI_ISteamMatchmakingServers_RefreshQuery( ISteamMatchmakingServers* self, HServerListRequest hRequest );
S_API bool SteamAPI_ISteamMatchmakingServers_IsRefreshing( ISteamMatchmakingServers* self, HServerListRequest hRequest );
S_API int SteamAPI_ISteamMatchmakingServers_GetServerCount( ISteamMatchmakingServers* self, HServerListRequest hRequest );
S_API void SteamAPI_ISteamMatchmakingServers_RefreshServer( ISteamMatchmakingServers* self, HServerListRequest hRequest, int iServer );
S_API HServerQuery SteamAPI_ISteamMatchmakingServers_PingServer( ISteamMatchmakingServers* self, uint32 unIP, uint16 usPort, ISteamMatchmakingPingResponse * pRequestServersResponse );
S_API HServerQuery SteamAPI_ISteamMatchmakingServers_PlayerDetails( ISteamMatchmakingServers* self, uint32 unIP, uint16 usPort, ISteamMatchmakingPlayersResponse * pRequestServersResponse );
S_API HServerQuery SteamAPI_ISteamMatchmakingServers_ServerRules( ISteamMatchmakingServers* self, uint32 unIP, uint16 usPort, ISteamMatchmakingRulesResponse * pRequestServersResponse );
S_API void SteamAPI_ISteamMatchmakingServers_CancelServerQuery( ISteamMatchmakingServers* self, HServerQuery hServerQuery );

// ISteamGameSearch

// A versioned accessor is exported by the library
S_API ISteamGameSearch *SteamAPI_SteamGameSearch_v001();
// Inline, unversioned accessor to get the current version.  Essentially the same as SteamGameSearch(), but using this ensures that you are using a matching library.
inline ISteamGameSearch *SteamAPI_SteamGameSearch() { return SteamAPI_SteamGameSearch_v001(); }
S_API EGameSearchErrorCode_t SteamAPI_ISteamGameSearch_AddGameSearchParams( ISteamGameSearch* self, const char * pchKeyToFind, const char * pchValuesToFind );
S_API EGameSearchErrorCode_t SteamAPI_ISteamGameSearch_SearchForGameWithLobby( ISteamGameSearch* self, uint64_steamid steamIDLobby, int nPlayerMin, int nPlayerMax );
S_API EGameSearchErrorCode_t SteamAPI_ISteamGameSearch_SearchForGameSolo( ISteamGameSearch* self, int nPlayerMin, int nPlayerMax );
S_API EGameSearchErrorCode_t SteamAPI_ISteamGameSearch_AcceptGame( ISteamGameSearch* self );
S_API EGameSearchErrorCode_t SteamAPI_ISteamGameSearch_DeclineGame( ISteamGameSearch* self );
S_API EGameSearchErrorCode_t SteamAPI_ISteamGameSearch_RetrieveConnectionDetails( ISteamGameSearch* self, uint64_steamid steamIDHost, char * pchConnectionDetails, int cubConnectionDetails );
S_API EGameSearchErrorCode_t SteamAPI_ISteamGameSearch_EndGameSearch( ISteamGameSearch* self );
S_API EGameSearchErrorCode_t SteamAPI_ISteamGameSearch_SetGameHostParams( ISteamGameSearch* self, const char * pchKey, const char * pchValue );
S_API EGameSearchErrorCode_t SteamAPI_ISteamGameSearch_SetConnectionDetails( ISteamGameSearch* self, const char * pchConnectionDetails, int cubConnectionDetails );
S_API EGameSearchErrorCode_t SteamAPI_ISteamGameSearch_RequestPlayersForGame( ISteamGameSearch* self, int nPlayerMin, int nPlayerMax, int nMaxTeamSize );
S_API EGameSearchErrorCode_t SteamAPI_ISteamGameSearch_HostConfirmGameStart( ISteamGameSearch* self, uint64 ullUniqueGameID );
S_API EGameSearchErrorCode_t SteamAPI_ISteamGameSearch_CancelRequestPlayersForGame( ISteamGameSearch* self );
S_API EGameSearchErrorCode_t SteamAPI_ISteamGameSearch_SubmitPlayerResult( ISteamGameSearch* self, uint64 ullUniqueGameID, uint64_steamid steamIDPlayer, EPlayerResult_t EPlayerResult );
S_API EGameSearchErrorCode_t SteamAPI_ISteamGameSearch_EndGame( ISteamGameSearch* self, uint64 ullUniqueGameID );

// ISteamParties

// A versioned accessor is exported by the library
S_API ISteamParties *SteamAPI_SteamParties_v002();
// Inline, unversioned accessor to get the current version.  Essentially the same as SteamParties(), but using this ensures that you are using a matching library.
inline ISteamParties *SteamAPI_SteamParties() { return SteamAPI_SteamParties_v002(); }
S_API uint32 SteamAPI_ISteamParties_GetNumActiveBeacons( ISteamParties* self );
S_API PartyBeaconID_t SteamAPI_ISteamParties_GetBeaconByIndex( ISteamParties* self, uint32 unIndex );
S_API bool SteamAPI_ISteamParties_GetBeaconDetails( ISteamParties* self, PartyBeaconID_t ulBeaconID, CSteamID * pSteamIDBeaconOwner, SteamPartyBeaconLocation_t * pLocation, char * pchMetadata, int cchMetadata );
S_API SteamAPICall_t SteamAPI_ISteamParties_JoinParty( ISteamParties* self, PartyBeaconID_t ulBeaconID );
S_API bool SteamAPI_ISteamParties_GetNumAvailableBeaconLocations( ISteamParties* self, uint32 * puNumLocations );
S_API bool SteamAPI_ISteamParties_GetAvailableBeaconLocations( ISteamParties* self, SteamPartyBeaconLocation_t * pLocationList, uint32 uMaxNumLocations );
S_API SteamAPICall_t SteamAPI_ISteamParties_CreateBeacon( ISteamParties* self, uint32 unOpenSlots, SteamPartyBeaconLocation_t * pBeaconLocation, const char * pchConnectString, const char * pchMetadata );
S_API void SteamAPI_ISteamParties_OnReservationCompleted( ISteamParties* self, PartyBeaconID_t ulBeacon, uint64_steamid steamIDUser );
S_API void SteamAPI_ISteamParties_CancelReservation( ISteamParties* self, PartyBeaconID_t ulBeacon, uint64_steamid steamIDUser );
S_API SteamAPICall_t SteamAPI_ISteamParties_ChangeNumOpenSlots( ISteamParties* self, PartyBeaconID_t ulBeacon, uint32 unOpenSlots );
S_API bool SteamAPI_ISteamParties_DestroyBeacon( ISteamParties* self, PartyBeaconID_t ulBeacon );
S_API bool SteamAPI_ISteamParties_GetBeaconLocationData( ISteamParties* self, SteamPartyBeaconLocation_t BeaconLocation, ESteamPartyBeaconLocationData eData, char * pchDataStringOut, int cchDataStringOut );

// ISteamRemoteStorage

// A versioned accessor is exported by the library
S_API ISteamRemoteStorage *SteamAPI_SteamRemoteStorage_v016();
// Inline, unversioned accessor to get the current version.  Essentially the same as SteamRemoteStorage(), but using this ensures that you are using a matching library.
inline ISteamRemoteStorage *SteamAPI_SteamRemoteStorage() { return SteamAPI_SteamRemoteStorage_v016(); }
S_API bool SteamAPI_ISteamRemoteStorage_FileWrite( ISteamRemoteStorage* self, const char * pchFile, const void * pvData, int32 cubData );
S_API int32 SteamAPI_ISteamRemoteStorage_FileRead( ISteamRemoteStorage* self, const char * pchFile, void * pvData, int32 cubDataToRead );
S_API SteamAPICall_t SteamAPI_ISteamRemoteStorage_FileWriteAsync( ISteamRemoteStorage* self, const char * pchFile, const void * pvData, uint32 cubData );
S_API SteamAPICall_t SteamAPI_ISteamRemoteStorage_FileReadAsync( ISteamRemoteStorage* self, const char * pchFile, uint32 nOffset, uint32 cubToRead );
S_API bool SteamAPI_ISteamRemoteStorage_FileReadAsyncComplete( ISteamRemoteStorage* self, SteamAPICall_t hReadCall, void * pvBuffer, uint32 cubToRead );
S_API bool SteamAPI_ISteamRemoteStorage_FileForget( ISteamRemoteStorage* self, const char * pchFile );
S_API bool SteamAPI_ISteamRemoteStorage_FileDelete( ISteamRemoteStorage* self, const char * pchFile );
S_API SteamAPICall_t SteamAPI_ISteamRemoteStorage_FileShare( ISteamRemoteStorage* self, const char * pchFile );
S_API bool SteamAPI_ISteamRemoteStorage_SetSyncPlatforms( ISteamRemoteStorage* self, const char * pchFile, ERemoteStoragePlatform eRemoteStoragePlatform );
S_API UGCFileWriteStreamHandle_t SteamAPI_ISteamRemoteStorage_FileWriteStreamOpen( ISteamRemoteStorage* self, const char * pchFile );
S_API bool SteamAPI_ISteamRemoteStorage_FileWriteStreamWriteChunk( ISteamRemoteStorage* self, UGCFileWriteStreamHandle_t writeHandle, const void * pvData, int32 cubData );
S_API bool SteamAPI_ISteamRemoteStorage_FileWriteStreamClose( ISteamRemoteStorage* self, UGCFileWriteStreamHandle_t writeHandle );
S_API bool SteamAPI_ISteamRemoteStorage_FileWriteStreamCancel( ISteamRemoteStorage* self, UGCFileWriteStreamHandle_t writeHandle );
S_API bool SteamAPI_ISteamRemoteStorage_FileExists( ISteamRemoteStorage* self, const char * pchFile );
S_API bool SteamAPI_ISteamRemoteStorage_FilePersisted( ISteamRemoteStorage* self, const char * pchFile );
S_API int32 SteamAPI_ISteamRemoteStorage_GetFileSize( ISteamRemoteStorage* self, const char * pchFile );
S_API int64 SteamAPI_ISteamRemoteStorage_GetFileTimestamp( ISteamRemoteStorage* self, const char * pchFile );
S_API ERemoteStoragePlatform SteamAPI_ISteamRemoteStorage_GetSyncPlatforms( ISteamRemoteStorage* self, const char * pchFile );
S_API int32 SteamAPI_ISteamRemoteStorage_GetFileCount( ISteamRemoteStorage* self );
S_API const char * SteamAPI_ISteamRemoteStorage_GetFileNameAndSize( ISteamRemoteStorage* self, int iFile, int32 * pnFileSizeInBytes );
S_API bool SteamAPI_ISteamRemoteStorage_GetQuota( ISteamRemoteStorage* self, uint64 * pnTotalBytes, uint64 * puAvailableBytes );
S_API bool SteamAPI_ISteamRemoteStorage_IsCloudEnabledForAccount( ISteamRemoteStorage* self );
S_API bool SteamAPI_ISteamRemoteStorage_IsCloudEnabledForApp( ISteamRemoteStorage* self );
S_API void SteamAPI_ISteamRemoteStorage_SetCloudEnabledForApp( ISteamRemoteStorage* self, bool bEnabled );
S_API SteamAPICall_t SteamAPI_ISteamRemoteStorage_UGCDownload( ISteamRemoteStorage* self, UGCHandle_t hContent, uint32 unPriority );
S_API bool SteamAPI_ISteamRemoteStorage_GetUGCDownloadProgress( ISteamRemoteStorage* self, UGCHandle_t hContent, int32 * pnBytesDownloaded, int32 * pnBytesExpected );
S_API bool SteamAPI_ISteamRemoteStorage_GetUGCDetails( ISteamRemoteStorage* self, UGCHandle_t hContent, AppId_t * pnAppID, char ** ppchName, int32 * pnFileSizeInBytes, CSteamID * pSteamIDOwner );
S_API int32 SteamAPI_ISteamRemoteStorage_UGCRead( ISteamRemoteStorage* self, UGCHandle_t hContent, void * pvData, int32 cubDataToRead, uint32 cOffset, EUGCReadAction eAction );
S_API int32 SteamAPI_ISteamRemoteStorage_GetCachedUGCCount( ISteamRemoteStorage* self );
S_API UGCHandle_t SteamAPI_ISteamRemoteStorage_GetCachedUGCHandle( ISteamRemoteStorage* self, int32 iCachedContent );
S_API SteamAPICall_t SteamAPI_ISteamRemoteStorage_PublishWorkshopFile( ISteamRemoteStorage* self, const char * pchFile, const char * pchPreviewFile, AppId_t nConsumerAppId, const char * pchTitle, const char * pchDescription, ERemoteStoragePublishedFileVisibility eVisibility, SteamParamStringArray_t * pTags, EWorkshopFileType eWorkshopFileType );
S_API PublishedFileUpdateHandle_t SteamAPI_ISteamRemoteStorage_CreatePublishedFileUpdateRequest( ISteamRemoteStorage* self, PublishedFileId_t unPublishedFileId );
S_API bool SteamAPI_ISteamRemoteStorage_UpdatePublishedFileFile( ISteamRemoteStorage* self, PublishedFileUpdateHandle_t updateHandle, const char * pchFile );
S_API bool SteamAPI_ISteamRemoteStorage_UpdatePublishedFilePreviewFile( ISteamRemoteStorage* self, PublishedFileUpdateHandle_t updateHandle, const char * pchPreviewFile );
S_API bool SteamAPI_ISteamRemoteStorage_UpdatePublishedFileTitle( ISteamRemoteStorage* self, PublishedFileUpdateHandle_t updateHandle, const char * pchTitle );
S_API bool SteamAPI_ISteamRemoteStorage_UpdatePublishedFileDescription( ISteamRemoteStorage* self, PublishedFileUpdateHandle_t updateHandle, const char * pchDescription );
S_API bool SteamAPI_ISteamRemoteStorage_UpdatePublishedFileVisibility( ISteamRemoteStorage* self, PublishedFileUpdateHandle_t updateHandle, ERemoteStoragePublishedFileVisibility eVisibility );
S_API bool SteamAPI_ISteamRemoteStorage_UpdatePublishedFileTags( ISteamRemoteStorage* self, PublishedFileUpdateHandle_t updateHandle, SteamParamStringArray_t * pTags );
S_API SteamAPICall_t SteamAPI_ISteamRemoteStorage_CommitPublishedFileUpdate( ISteamRemoteStorage* self, PublishedFileUpdateHandle_t updateHandle );
S_API SteamAPICall_t SteamAPI_ISteamRemoteStorage_GetPublishedFileDetails( ISteamRemoteStorage* self, PublishedFileId_t unPublishedFileId, uint32 unMaxSecondsOld );
S_API SteamAPICall_t SteamAPI_ISteamRemoteStorage_DeletePublishedFile( ISteamRemoteStorage* self, PublishedFileId_t unPublishedFileId );
S_API SteamAPICall_t SteamAPI_ISteamRemoteStorage_EnumerateUserPublishedFiles( ISteamRemoteStorage* self, uint32 unStartIndex );
S_API SteamAPICall_t SteamAPI_ISteamRemoteStorage_SubscribePublishedFile( ISteamRemoteStorage* self, PublishedFileId_t unPublishedFileId );
S_API SteamAPICall_t SteamAPI_ISteamRemoteStorage_EnumerateUserSubscribedFiles( ISteamRemoteStorage* self, uint32 unStartIndex );
S_API SteamAPICall_t SteamAPI_ISteamRemoteStorage_UnsubscribePublishedFile( ISteamRemoteStorage* self, PublishedFileId_t unPublishedFileId );
S_API bool SteamAPI_ISteamRemoteStorage_UpdatePublishedFileSetChangeDescription( ISteamRemoteStorage* self, PublishedFileUpdateHandle_t updateHandle, const char * pchChangeDescription );
S_API SteamAPICall_t SteamAPI_ISteamRemoteStorage_GetPublishedItemVoteDetails( ISteamRemoteStorage* self, PublishedFileId_t unPublishedFileId );
S_API SteamAPICall_t SteamAPI_ISteamRemoteStorage_UpdateUserPublishedItemVote( ISteamRemoteStorage* self, PublishedFileId_t unPublishedFileId, bool bVoteUp );
S_API SteamAPICall_t SteamAPI_ISteamRemoteStorage_GetUserPublishedItemVoteDetails( ISteamRemoteStorage* self, PublishedFileId_t unPublishedFileId );
S_API SteamAPICall_t SteamAPI_ISteamRemoteStorage_EnumerateUserSharedWorkshopFiles( ISteamRemoteStorage* self, uint64_steamid steamId, uint32 unStartIndex, SteamParamStringArray_t * pRequiredTags, SteamParamStringArray_t * pExcludedTags );
S_API SteamAPICall_t SteamAPI_ISteamRemoteStorage_PublishVideo( ISteamRemoteStorage* self, EWorkshopVideoProvider eVideoProvider, const char * pchVideoAccount, const char * pchVideoIdentifier, const char * pchPreviewFile, AppId_t nConsumerAppId, const char * pchTitle, const char * pchDescription, ERemoteStoragePublishedFileVisibility eVisibility, SteamParamStringArray_t * pTags );
S_API SteamAPICall_t SteamAPI_ISteamRemoteStorage_SetUserPublishedFileAction( ISteamRemoteStorage* self, PublishedFileId_t unPublishedFileId, EWorkshopFileAction eAction );
S_API SteamAPICall_t SteamAPI_ISteamRemoteStorage_EnumeratePublishedFilesByUserAction( ISteamRemoteStorage* self, EWorkshopFileAction eAction, uint32 unStartIndex );
S_API SteamAPICall_t SteamAPI_ISteamRemoteStorage_EnumeratePublishedWorkshopFiles( ISteamRemoteStorage* self, EWorkshopEnumerationType eEnumerationType, uint32 unStartIndex, uint32 unCount, uint32 unDays, SteamParamStringArray_t * pTags, SteamParamStringArray_t * pUserTags );
S_API SteamAPICall_t SteamAPI_ISteamRemoteStorage_UGCDownloadToLocation( ISteamRemoteStorage* self, UGCHandle_t hContent, const char * pchLocation, uint32 unPriority );
S_API int32 SteamAPI_ISteamRemoteStorage_GetLocalFileChangeCount( ISteamRemoteStorage* self );
S_API const char * SteamAPI_ISteamRemoteStorage_GetLocalFileChange( ISteamRemoteStorage* self, int iFile, ERemoteStorageLocalFileChange * pEChangeType, ERemoteStorageFilePathType * pEFilePathType );
S_API bool SteamAPI_ISteamRemoteStorage_BeginFileWriteBatch( ISteamRemoteStorage* self );
S_API bool SteamAPI_ISteamRemoteStorage_EndFileWriteBatch( ISteamRemoteStorage* self );

// ISteamUserStats

// A versioned accessor is exported by the library
S_API ISteamUserStats *SteamAPI_SteamUserStats_v012();
// Inline, unversioned accessor to get the current version.  Essentially the same as SteamUserStats(), but using this ensures that you are using a matching library.
inline ISteamUserStats *SteamAPI_SteamUserStats() { return SteamAPI_SteamUserStats_v012(); }
S_API bool SteamAPI_ISteamUserStats_RequestCurrentStats( ISteamUserStats* self );
S_API bool SteamAPI_ISteamUserStats_GetStatInt32( ISteamUserStats* self, const char * pchName, int32 * pData );
S_API bool SteamAPI_ISteamUserStats_GetStatFloat( ISteamUserStats* self, const char * pchName, float * pData );
S_API bool SteamAPI_ISteamUserStats_SetStatInt32( ISteamUserStats* self, const char * pchName, int32 nData );
S_API bool SteamAPI_ISteamUserStats_SetStatFloat( ISteamUserStats* self, const char * pchName, float fData );
S_API bool SteamAPI_ISteamUserStats_UpdateAvgRateStat( ISteamUserStats* self, const char * pchName, float flCountThisSession, double dSessionLength );
S_API bool SteamAPI_ISteamUserStats_GetAchievement( ISteamUserStats* self, const char * pchName, bool * pbAchieved );
S_API bool SteamAPI_ISteamUserStats_SetAchievement( ISteamUserStats* self, const char * pchName );
S_API bool SteamAPI_ISteamUserStats_ClearAchievement( ISteamUserStats* self, const char * pchName );
S_API bool SteamAPI_ISteamUserStats_GetAchievementAndUnlockTime( ISteamUserStats* self, const char * pchName, bool * pbAchieved, uint32 * punUnlockTime );
S_API bool SteamAPI_ISteamUserStats_StoreStats( ISteamUserStats* self );
S_API int SteamAPI_ISteamUserStats_GetAchievementIcon( ISteamUserStats* self, const char * pchName );
S_API const char * SteamAPI_ISteamUserStats_GetAchievementDisplayAttribute( ISteamUserStats* self, const char * pchName, const char * pchKey );
S_API bool SteamAPI_ISteamUserStats_IndicateAchievementProgress( ISteamUserStats* self, const char * pchName, uint32 nCurProgress, uint32 nMaxProgress );
S_API uint32 SteamAPI_ISteamUserStats_GetNumAchievements( ISteamUserStats* self );
S_API const char * SteamAPI_ISteamUserStats_GetAchievementName( ISteamUserStats* self, uint32 iAchievement );
S_API SteamAPICall_t SteamAPI_ISteamUserStats_RequestUserStats( ISteamUserStats* self, uint64_steamid steamIDUser );
S_API bool SteamAPI_ISteamUserStats_GetUserStatInt32( ISteamUserStats* self, uint64_steamid steamIDUser, const char * pchName, int32 * pData );
S_API bool SteamAPI_ISteamUserStats_GetUserStatFloat( ISteamUserStats* self, uint64_steamid steamIDUser, const char * pchName, float * pData );
S_API bool SteamAPI_ISteamUserStats_GetUserAchievement( ISteamUserStats* self, uint64_steamid steamIDUser, const char * pchName, bool * pbAchieved );
S_API bool SteamAPI_ISteamUserStats_GetUserAchievementAndUnlockTime( ISteamUserStats* self, uint64_steamid steamIDUser, const char * pchName, bool * pbAchieved, uint32 * punUnlockTime );
S_API bool SteamAPI_ISteamUserStats_ResetAllStats( ISteamUserStats* self, bool bAchievementsToo );
S_API SteamAPICall_t SteamAPI_ISteamUserStats_FindOrCreateLeaderboard( ISteamUserStats* self, const char * pchLeaderboardName, ELeaderboardSortMethod eLeaderboardSortMethod, ELeaderboardDisplayType eLeaderboardDisplayType );
S_API SteamAPICall_t SteamAPI_ISteamUserStats_FindLeaderboard( ISteamUserStats* self, const char * pchLeaderboardName );
S_API const char * SteamAPI_ISteamUserStats_GetLeaderboardName( ISteamUserStats* self, SteamLeaderboard_t hSteamLeaderboard );
S_API int SteamAPI_ISteamUserStats_GetLeaderboardEntryCount( ISteamUserStats* self, SteamLeaderboard_t hSteamLeaderboard );
S_API ELeaderboardSortMethod SteamAPI_ISteamUserStats_GetLeaderboardSortMethod( ISteamUserStats* self, SteamLeaderboard_t hSteamLeaderboard );
S_API ELeaderboardDisplayType SteamAPI_ISteamUserStats_GetLeaderboardDisplayType( ISteamUserStats* self, SteamLeaderboard_t hSteamLeaderboard );
S_API SteamAPICall_t SteamAPI_ISteamUserStats_DownloadLeaderboardEntries( ISteamUserStats* self, SteamLeaderboard_t hSteamLeaderboard, ELeaderboardDataRequest eLeaderboardDataRequest, int nRangeStart, int nRangeEnd );
S_API SteamAPICall_t SteamAPI_ISteamUserStats_DownloadLeaderboardEntriesForUsers( ISteamUserStats* self, SteamLeaderboard_t hSteamLeaderboard, CSteamID * prgUsers, int cUsers );
S_API bool SteamAPI_ISteamUserStats_GetDownloadedLeaderboardEntry( ISteamUserStats* self, SteamLeaderboardEntries_t hSteamLeaderboardEntries, int index, LeaderboardEntry_t * pLeaderboardEntry, int32 * pDetails, int cDetailsMax );
S_API SteamAPICall_t SteamAPI_ISteamUserStats_UploadLeaderboardScore( ISteamUserStats* self, SteamLeaderboard_t hSteamLeaderboard, ELeaderboardUploadScoreMethod eLeaderboardUploadScoreMethod, int32 nScore, const int32 * pScoreDetails, int cScoreDetailsCount );
S_API SteamAPICall_t SteamAPI_ISteamUserStats_AttachLeaderboardUGC( ISteamUserStats* self, SteamLeaderboard_t hSteamLeaderboard, UGCHandle_t hUGC );
S_API SteamAPICall_t SteamAPI_ISteamUserStats_GetNumberOfCurrentPlayers( ISteamUserStats* self );
S_API SteamAPICall_t SteamAPI_ISteamUserStats_RequestGlobalAchievementPercentages( ISteamUserStats* self );
S_API int SteamAPI_ISteamUserStats_GetMostAchievedAchievementInfo( ISteamUserStats* self, char * pchName, uint32 unNameBufLen, float * pflPercent, bool * pbAchieved );
S_API int SteamAPI_ISteamUserStats_GetNextMostAchievedAchievementInfo( ISteamUserStats* self, int iIteratorPrevious, char * pchName, uint32 unNameBufLen, float * pflPercent, bool * pbAchieved );
S_API bool SteamAPI_ISteamUserStats_GetAchievementAchievedPercent( ISteamUserStats* self, const char * pchName, float * pflPercent );
S_API SteamAPICall_t SteamAPI_ISteamUserStats_RequestGlobalStats( ISteamUserStats* self, int nHistoryDays );
S_API bool SteamAPI_ISteamUserStats_GetGlobalStatInt64( ISteamUserStats* self, const char * pchStatName, int64 * pData );
S_API bool SteamAPI_ISteamUserStats_GetGlobalStatDouble( ISteamUserStats* self, const char * pchStatName, double * pData );
S_API int32 SteamAPI_ISteamUserStats_GetGlobalStatHistoryInt64( ISteamUserStats* self, const char * pchStatName, int64 * pData, uint32 cubData );
S_API int32 SteamAPI_ISteamUserStats_GetGlobalStatHistoryDouble( ISteamUserStats* self, const char * pchStatName, double * pData, uint32 cubData );
S_API bool SteamAPI_ISteamUserStats_GetAchievementProgressLimitsInt32( ISteamUserStats* self, const char * pchName, int32 * pnMinProgress, int32 * pnMaxProgress );
S_API bool SteamAPI_ISteamUserStats_GetAchievementProgressLimitsFloat( ISteamUserStats* self, const char * pchName, float * pfMinProgress, float * pfMaxProgress );

// ISteamApps

// A versioned accessor is exported by the library
S_API ISteamApps *SteamAPI_SteamApps_v008();
// Inline, unversioned accessor to get the current version.  Essentially the same as SteamApps(), but using this ensures that you are using a matching library.
inline ISteamApps *SteamAPI_SteamApps() { return SteamAPI_SteamApps_v008(); }
S_API bool SteamAPI_ISteamApps_BIsSubscribed( ISteamApps* self );
S_API bool SteamAPI_ISteamApps_BIsLowViolence( ISteamApps* self );
S_API bool SteamAPI_ISteamApps_BIsCybercafe( ISteamApps* self );
S_API bool SteamAPI_ISteamApps_BIsVACBanned( ISteamApps* self );
S_API const char * SteamAPI_ISteamApps_GetCurrentGameLanguage( ISteamApps* self );
S_API const char * SteamAPI_ISteamApps_GetAvailableGameLanguages( ISteamApps* self );
S_API bool SteamAPI_ISteamApps_BIsSubscribedApp( ISteamApps* self, AppId_t appID );
S_API bool SteamAPI_ISteamApps_BIsDlcInstalled( ISteamApps* self, AppId_t appID );
S_API uint32 SteamAPI_ISteamApps_GetEarliestPurchaseUnixTime( ISteamApps* self, AppId_t nAppID );
S_API bool SteamAPI_ISteamApps_BIsSubscribedFromFreeWeekend( ISteamApps* self );
S_API int SteamAPI_ISteamApps_GetDLCCount( ISteamApps* self );
S_API bool SteamAPI_ISteamApps_BGetDLCDataByIndex( ISteamApps* self, int iDLC, AppId_t * pAppID, bool * pbAvailable, char * pchName, int cchNameBufferSize );
S_API void SteamAPI_ISteamApps_InstallDLC( ISteamApps* self, AppId_t nAppID );
S_API void SteamAPI_ISteamApps_UninstallDLC( ISteamApps* self, AppId_t nAppID );
S_API void SteamAPI_ISteamApps_RequestAppProofOfPurchaseKey( ISteamApps* self, AppId_t nAppID );
S_API bool SteamAPI_ISteamApps_GetCurrentBetaName( ISteamApps* self, char * pchName, int cchNameBufferSize );
S_API bool SteamAPI_ISteamApps_MarkContentCorrupt( ISteamApps* self, bool bMissingFilesOnly );
S_API uint32 SteamAPI_ISteamApps_GetInstalledDepots( ISteamApps* self, AppId_t appID, DepotId_t * pvecDepots, uint32 cMaxDepots );
S_API uint32 SteamAPI_ISteamApps_GetAppInstallDir( ISteamApps* self, AppId_t appID, char * pchFolder, uint32 cchFolderBufferSize );
S_API bool SteamAPI_ISteamApps_BIsAppInstalled( ISteamApps* self, AppId_t appID );
S_API uint64_steamid SteamAPI_ISteamApps_GetAppOwner( ISteamApps* self );
S_API const char * SteamAPI_ISteamApps_GetLaunchQueryParam( ISteamApps* self, const char * pchKey );
S_API bool SteamAPI_ISteamApps_GetDlcDownloadProgress( ISteamApps* self, AppId_t nAppID, uint64 * punBytesDownloaded, uint64 * punBytesTotal );
S_API int SteamAPI_ISteamApps_GetAppBuildId( ISteamApps* self );
S_API void SteamAPI_ISteamApps_RequestAllProofOfPurchaseKeys( ISteamApps* self );
S_API SteamAPICall_t SteamAPI_ISteamApps_GetFileDetails( ISteamApps* self, const char * pszFileName );
S_API int SteamAPI_ISteamApps_GetLaunchCommandLine( ISteamApps* self, char * pszCommandLine, int cubCommandLine );
S_API bool SteamAPI_ISteamApps_BIsSubscribedFromFamilySharing( ISteamApps* self );
S_API bool SteamAPI_ISteamApps_BIsTimedTrial( ISteamApps* self, uint32 * punSecondsAllowed, uint32 * punSecondsPlayed );
S_API bool SteamAPI_ISteamApps_SetDlcContext( ISteamApps* self, AppId_t nAppID );
S_API int SteamAPI_ISteamApps_GetNumBetas( ISteamApps* self, AppId_t unAppID, int * pnAvailable, int * pnPrivate );
S_API bool SteamAPI_ISteamApps_GetBetaInfo( ISteamApps* self, AppId_t unAppID, int iBetaIndex, uint32 * punFlags, uint32 * punBuildID, char * pchBetaName, int cchBetaName, char * pchDescription, int cchDescription );
S_API bool SteamAPI_ISteamApps_SetActiveBeta( ISteamApps* self, AppId_t unAppID, const char * pchBetaName );

// ISteamNetworking

// A versioned accessor is exported by the library
S_API ISteamNetworking *SteamAPI_SteamNetworking_v006();
// Inline, unversioned accessor to get the current version.  Essentially the same as SteamNetworking(), but using this ensures that you are using a matching library.
inline ISteamNetworking *SteamAPI_SteamNetworking() { return SteamAPI_SteamNetworking_v006(); }

// A versioned accessor is exported by the library
S_API ISteamNetworking *SteamAPI_SteamGameServerNetworking_v006();
// Inline, unversioned accessor to get the current version.  Essentially the same as SteamGameServerNetworking(), but using this ensures that you are using a matching library.
inline ISteamNetworking *SteamAPI_SteamGameServerNetworking() { return SteamAPI_SteamGameServerNetworking_v006(); }
S_API bool SteamAPI_ISteamNetworking_SendP2PPacket( ISteamNetworking* self, uint64_steamid steamIDRemote, const void * pubData, uint32 cubData, EP2PSend eP2PSendType, int nChannel );
S_API bool SteamAPI_ISteamNetworking_IsP2PPacketAvailable( ISteamNetworking* self, uint32 * pcubMsgSize, int nChannel );
S_API bool SteamAPI_ISteamNetworking_ReadP2PPacket( ISteamNetworking* self, void * pubDest, uint32 cubDest, uint32 * pcubMsgSize, CSteamID * psteamIDRemote, int nChannel );
S_API bool SteamAPI_ISteamNetworking_AcceptP2PSessionWithUser( ISteamNetworking* self, uint64_steamid steamIDRemote );
S_API bool SteamAPI_ISteamNetworking_CloseP2PSessionWithUser( ISteamNetworking* self, uint64_steamid steamIDRemote );
S_API bool SteamAPI_ISteamNetworking_CloseP2PChannelWithUser( ISteamNetworking* self, uint64_steamid steamIDRemote, int nChannel );
S_API bool SteamAPI_ISteamNetworking_GetP2PSessionState( ISteamNetworking* self, uint64_steamid steamIDRemote, P2PSessionState_t * pConnectionState );
S_API bool SteamAPI_ISteamNetworking_AllowP2PPacketRelay( ISteamNetworking* self, bool bAllow );
S_API SNetListenSocket_t SteamAPI_ISteamNetworking_CreateListenSocket( ISteamNetworking* self, int nVirtualP2PPort, SteamIPAddress_t nIP, uint16 nPort, bool bAllowUseOfPacketRelay );
S_API SNetSocket_t SteamAPI_ISteamNetworking_CreateP2PConnectionSocket( ISteamNetworking* self, uint64_steamid steamIDTarget, int nVirtualPort, int nTimeoutSec, bool bAllowUseOfPacketRelay );
S_API SNetSocket_t SteamAPI_ISteamNetworking_CreateConnectionSocket( ISteamNetworking* self, SteamIPAddress_t nIP, uint16 nPort, int nTimeoutSec );
S_API bool SteamAPI_ISteamNetworking_DestroySocket( ISteamNetworking* self, SNetSocket_t hSocket, bool bNotifyRemoteEnd );
S_API bool SteamAPI_ISteamNetworking_DestroyListenSocket( ISteamNetworking* self, SNetListenSocket_t hSocket, bool bNotifyRemoteEnd );
S_API bool SteamAPI_ISteamNetworking_SendDataOnSocket( ISteamNetworking* self, SNetSocket_t hSocket, void * pubData, uint32 cubData, bool bReliable );
S_API bool SteamAPI_ISteamNetworking_IsDataAvailableOnSocket( ISteamNetworking* self, SNetSocket_t hSocket, uint32 * pcubMsgSize );
S_API bool SteamAPI_ISteamNetworking_RetrieveDataFromSocket( ISteamNetworking* self, SNetSocket_t hSocket, void * pubDest, uint32 cubDest, uint32 * pcubMsgSize );
S_API bool SteamAPI_ISteamNetworking_IsDataAvailable( ISteamNetworking* self, SNetListenSocket_t hListenSocket, uint32 * pcubMsgSize, SNetSocket_t * phSocket );
S_API bool SteamAPI_ISteamNetworking_RetrieveData( ISteamNetworking* self, SNetListenSocket_t hListenSocket, void * pubDest, uint32 cubDest, uint32 * pcubMsgSize, SNetSocket_t * phSocket );
S_API bool SteamAPI_ISteamNetworking_GetSocketInfo( ISteamNetworking* self, SNetSocket_t hSocket, CSteamID * pSteamIDRemote, int * peSocketStatus, SteamIPAddress_t * punIPRemote, uint16 * punPortRemote );
S_API bool SteamAPI_ISteamNetworking_GetListenSocketInfo( ISteamNetworking* self, SNetListenSocket_t hListenSocket, SteamIPAddress_t * pnIP, uint16 * pnPort );
S_API ESNetSocketConnectionType SteamAPI_ISteamNetworking_GetSocketConnectionType( ISteamNetworking* self, SNetSocket_t hSocket );
S_API int SteamAPI_ISteamNetworking_GetMaxPacketSize( ISteamNetworking* self, SNetSocket_t hSocket );

// ISteamScreenshots

// A versioned accessor is exported by the library
S_API ISteamScreenshots *SteamAPI_SteamScreenshots_v003();
// Inline, unversioned accessor to get the current version.  Essentially the same as SteamScreenshots(), but using this ensures that you are using a matching library.
inline ISteamScreenshots *SteamAPI_SteamScreenshots() { return SteamAPI_SteamScreenshots_v003(); }
S_API ScreenshotHandle SteamAPI_ISteamScreenshots_WriteScreenshot( ISteamScreenshots* self, void * pubRGB, uint32 cubRGB, int nWidth, int nHeight );
S_API ScreenshotHandle SteamAPI_ISteamScreenshots_AddScreenshotToLibrary( ISteamScreenshots* self, const char * pchFilename, const char * pchThumbnailFilename, int nWidth, int nHeight );
S_API void SteamAPI_ISteamScreenshots_TriggerScreenshot( ISteamScreenshots* self );
S_API void SteamAPI_ISteamScreenshots_HookScreenshots( ISteamScreenshots* self, bool bHook );
S_API bool SteamAPI_ISteamScreenshots_SetLocation( ISteamScreenshots* self, ScreenshotHandle hScreenshot, const char * pchLocation );
S_API bool SteamAPI_ISteamScreenshots_TagUser( ISteamScreenshots* self, ScreenshotHandle hScreenshot, uint64_steamid steamID );
S_API bool SteamAPI_ISteamScreenshots_TagPublishedFile( ISteamScreenshots* self, ScreenshotHandle hScreenshot, PublishedFileId_t unPublishedFileID );
S_API bool SteamAPI_ISteamScreenshots_IsScreenshotsHooked( ISteamScreenshots* self );
S_API ScreenshotHandle SteamAPI_ISteamScreenshots_AddVRScreenshotToLibrary( ISteamScreenshots* self, EVRScreenshotType eType, const char * pchFilename, const char * pchVRFilename );

// ISteamMusic

// A versioned accessor is exported by the library
S_API ISteamMusic *SteamAPI_SteamMusic_v001();
// Inline, unversioned accessor to get the current version.  Essentially the same as SteamMusic(), but using this ensures that you are using a matching library.
inline ISteamMusic *SteamAPI_SteamMusic() { return SteamAPI_SteamMusic_v001(); }
S_API bool SteamAPI_ISteamMusic_BIsEnabled( ISteamMusic* self );
S_API bool SteamAPI_ISteamMusic_BIsPlaying( ISteamMusic* self );
S_API AudioPlayback_Status SteamAPI_ISteamMusic_GetPlaybackStatus( ISteamMusic* self );
S_API void SteamAPI_ISteamMusic_Play( ISteamMusic* self );
S_API void SteamAPI_ISteamMusic_Pause( ISteamMusic* self );
S_API void SteamAPI_ISteamMusic_PlayPrevious( ISteamMusic* self );
S_API void SteamAPI_ISteamMusic_PlayNext( ISteamMusic* self );
S_API void SteamAPI_ISteamMusic_SetVolume( ISteamMusic* self, float flVolume );
S_API float SteamAPI_ISteamMusic_GetVolume( ISteamMusic* self );

// ISteamMusicRemote

// A versioned accessor is exported by the library
S_API ISteamMusicRemote *SteamAPI_SteamMusicRemote_v001();
// Inline, unversioned accessor to get the current version.  Essentially the same as SteamMusicRemote(), but using this ensures that you are using a matching library.
inline ISteamMusicRemote *SteamAPI_SteamMusicRemote() { return SteamAPI_SteamMusicRemote_v001(); }
S_API bool SteamAPI_ISteamMusicRemote_RegisterSteamMusicRemote( ISteamMusicRemote* self, const char * pchName );
S_API bool SteamAPI_ISteamMusicRemote_DeregisterSteamMusicRemote( ISteamMusicRemote* self );
S_API bool SteamAPI_ISteamMusicRemote_BIsCurrentMusicRemote( ISteamMusicRemote* self );
S_API bool SteamAPI_ISteamMusicRemote_BActivationSuccess( ISteamMusicRemote* self, bool bValue );
S_API bool SteamAPI_ISteamMusicRemote_SetDisplayName( ISteamMusicRemote* self, const char * pchDisplayName );
S_API bool SteamAPI_ISteamMusicRemote_SetPNGIcon_64x64( ISteamMusicRemote* self, void * pvBuffer, uint32 cbBufferLength );
S_API bool SteamAPI_ISteamMusicRemote_EnablePlayPrevious( ISteamMusicRemote* self, bool bValue );
S_API bool SteamAPI_ISteamMusicRemote_EnablePlayNext( ISteamMusicRemote* self, bool bValue );
S_API bool SteamAPI_ISteamMusicRemote_EnableShuffled( ISteamMusicRemote* self, bool bValue );
S_API bool SteamAPI_ISteamMusicRemote_EnableLooped( ISteamMusicRemote* self, bool bValue );
S_API bool SteamAPI_ISteamMusicRemote_EnableQueue( ISteamMusicRemote* self, bool bValue );
S_API bool SteamAPI_ISteamMusicRemote_EnablePlaylists( ISteamMusicRemote* self, bool bValue );
S_API bool SteamAPI_ISteamMusicRemote_UpdatePlaybackStatus( ISteamMusicRemote* self, AudioPlayback_Status nStatus );
S_API bool SteamAPI_ISteamMusicRemote_UpdateShuffled( ISteamMusicRemote* self, bool bValue );
S_API bool SteamAPI_ISteamMusicRemote_UpdateLooped( ISteamMusicRemote* self, bool bValue );
S_API bool SteamAPI_ISteamMusicRemote_UpdateVolume( ISteamMusicRemote* self, float flValue );
S_API bool SteamAPI_ISteamMusicRemote_CurrentEntryWillChange( ISteamMusicRemote* self );
S_API bool SteamAPI_ISteamMusicRemote_CurrentEntryIsAvailable( ISteamMusicRemote* self, bool bAvailable );
S_API bool SteamAPI_ISteamMusicRemote_UpdateCurrentEntryText( ISteamMusicRemote* self, const char * pchText );
S_API bool SteamAPI_ISteamMusicRemote_UpdateCurrentEntryElapsedSeconds( ISteamMusicRemote* self, int nValue );
S_API bool SteamAPI_ISteamMusicRemote_UpdateCurrentEntryCoverArt( ISteamMusicRemote* self, void * pvBuffer, uint32 cbBufferLength );
S_API bool SteamAPI_ISteamMusicRemote_CurrentEntryDidChange( ISteamMusicRemote* self );
S_API bool SteamAPI_ISteamMusicRemote_QueueWillChange( ISteamMusicRemote* self );
S_API bool SteamAPI_ISteamMusicRemote_ResetQueueEntries( ISteamMusicRemote* self );
S_API bool SteamAPI_ISteamMusicRemote_SetQueueEntry( ISteamMusicRemote* self, int nID, int nPosition, const char * pchEntryText );
S_API bool SteamAPI_ISteamMusicRemote_SetCurrentQueueEntry( ISteamMusicRemote* self, int nID );
S_API bool SteamAPI_ISteamMusicRemote_QueueDidChange( ISteamMusicRemote* self );
S_API bool SteamAPI_ISteamMusicRemote_PlaylistWillChange( ISteamMusicRemote* self );
S_API bool SteamAPI_ISteamMusicRemote_ResetPlaylistEntries( ISteamMusicRemote* self );
S_API bool SteamAPI_ISteamMusicRemote_SetPlaylistEntry( ISteamMusicRemote* self, int nID, int nPosition, const char * pchEntryText );
S_API bool SteamAPI_ISteamMusicRemote_SetCurrentPlaylistEntry( ISteamMusicRemote* self, int nID );
S_API bool SteamAPI_ISteamMusicRemote_PlaylistDidChange( ISteamMusicRemote* self );

// ISteamHTTP

// A versioned accessor is exported by the library
S_API ISteamHTTP *SteamAPI_SteamHTTP_v003();
// Inline, unversioned accessor to get the current version.  Essentially the same as SteamHTTP(), but using this ensures that you are using a matching library.
inline ISteamHTTP *SteamAPI_SteamHTTP() { return SteamAPI_SteamHTTP_v003(); }

// A versioned accessor is exported by the library
S_API ISteamHTTP *SteamAPI_SteamGameServerHTTP_v003();
// Inline, unversioned accessor to get the current version.  Essentially the same as SteamGameServerHTTP(), but using this ensures that you are using a matching library.
inline ISteamHTTP *SteamAPI_SteamGameServerHTTP() { return SteamAPI_SteamGameServerHTTP_v003(); }
S_API HTTPRequestHandle SteamAPI_ISteamHTTP_CreateHTTPRequest( ISteamHTTP* self, EHTTPMethod eHTTPRequestMethod, const char * pchAbsoluteURL );
S_API bool SteamAPI_ISteamHTTP_SetHTTPRequestContextValue( ISteamHTTP* self, HTTPRequestHandle hRequest, uint64 ulContextValue );
S_API bool SteamAPI_ISteamHTTP_SetHTTPRequestNetworkActivityTimeout( ISteamHTTP* self, HTTPRequestHandle hRequest, uint32 unTimeoutSeconds );
S_API bool SteamAPI_ISteamHTTP_SetHTTPRequestHeaderValue( ISteamHTTP* self, HTTPRequestHandle hRequest, const char * pchHeaderName, const char * pchHeaderValue );
S_API bool SteamAPI_ISteamHTTP_SetHTTPRequestGetOrPostParameter( ISteamHTTP* self, HTTPRequestHandle hRequest, const char * pchParamName, const char * pchParamValue );
S_API bool SteamAPI_ISteamHTTP_SendHTTPRequest( ISteamHTTP* self, HTTPRequestHandle hRequest, SteamAPICall_t * pCallHandle );
S_API bool SteamAPI_ISteamHTTP_SendHTTPRequestAndStreamResponse( ISteamHTTP* self, HTTPRequestHandle hRequest, SteamAPICall_t * pCallHandle );
S_API bool SteamAPI_ISteamHTTP_DeferHTTPRequest( ISteamHTTP* self, HTTPRequestHandle hRequest );
S_API bool SteamAPI_ISteamHTTP_PrioritizeHTTPRequest( ISteamHTTP* self, HTTPRequestHandle hRequest );
S_API bool SteamAPI_ISteamHTTP_GetHTTPResponseHeaderSize( ISteamHTTP* self, HTTPRequestHandle hRequest, const char * pchHeaderName, uint32 * unResponseHeaderSize );
S_API bool SteamAPI_ISteamHTTP_GetHTTPResponseHeaderValue( ISteamHTTP* self, HTTPRequestHandle hRequest, const char * pchHeaderName, uint8 * pHeaderValueBuffer, uint32 unBufferSize );
S_API bool SteamAPI_ISteamHTTP_GetHTTPResponseBodySize( ISteamHTTP* self, HTTPRequestHandle hRequest, uint32 * unBodySize );
S_API bool SteamAPI_ISteamHTTP_GetHTTPResponseBodyData( ISteamHTTP* self, HTTPRequestHandle hRequest, uint8 * pBodyDataBuffer, uint32 unBufferSize );
S_API bool SteamAPI_ISteamHTTP_GetHTTPStreamingResponseBodyData( ISteamHTTP* self, HTTPRequestHandle hRequest, uint32 cOffset, uint8 * pBodyDataBuffer, uint32 unBufferSize );
S_API bool SteamAPI_ISteamHTTP_ReleaseHTTPRequest( ISteamHTTP* self, HTTPRequestHandle hRequest );
S_API bool SteamAPI_ISteamHTTP_GetHTTPDownloadProgressPct( ISteamHTTP* self, HTTPRequestHandle hRequest, float * pflPercentOut );
S_API bool SteamAPI_ISteamHTTP_SetHTTPRequestRawPostBody( ISteamHTTP* self, HTTPRequestHandle hRequest, const char * pchContentType, uint8 * pubBody, uint32 unBodyLen );
S_API HTTPCookieContainerHandle SteamAPI_ISteamHTTP_CreateCookieContainer( ISteamHTTP* self, bool bAllowResponsesToModify );
S_API bool SteamAPI_ISteamHTTP_ReleaseCookieContainer( ISteamHTTP* self, HTTPCookieContainerHandle hCookieContainer );
S_API bool SteamAPI_ISteamHTTP_SetCookie( ISteamHTTP* self, HTTPCookieContainerHandle hCookieContainer, const char * pchHost, const char * pchUrl, const char * pchCookie );
S_API bool SteamAPI_ISteamHTTP_SetHTTPRequestCookieContainer( ISteamHTTP* self, HTTPRequestHandle hRequest, HTTPCookieContainerHandle hCookieContainer );
S_API bool SteamAPI_ISteamHTTP_SetHTTPRequestUserAgentInfo( ISteamHTTP* self, HTTPRequestHandle hRequest, const char * pchUserAgentInfo );
S_API bool SteamAPI_ISteamHTTP_SetHTTPRequestRequiresVerifiedCertificate( ISteamHTTP* self, HTTPRequestHandle hRequest, bool bRequireVerifiedCertificate );
S_API bool SteamAPI_ISteamHTTP_SetHTTPRequestAbsoluteTimeoutMS( ISteamHTTP* self, HTTPRequestHandle hRequest, uint32 unMilliseconds );
S_API bool SteamAPI_ISteamHTTP_GetHTTPRequestWasTimedOut( ISteamHTTP* self, HTTPRequestHandle hRequest, bool * pbWasTimedOut );

// ISteamInput

// A versioned accessor is exported by the library
S_API ISteamInput *SteamAPI_SteamInput_v006();
// Inline, unversioned accessor to get the current version.  Essentially the same as SteamInput(), but using this ensures that you are using a matching library.
inline ISteamInput *SteamAPI_SteamInput() { return SteamAPI_SteamInput_v006(); }
S_API bool SteamAPI_ISteamInput_Init( ISteamInput* self, bool bExplicitlyCallRunFrame );
S_API bool SteamAPI_ISteamInput_Shutdown( ISteamInput* self );
S_API bool SteamAPI_ISteamInput_SetInputActionManifestFilePath( ISteamInput* self, const char * pchInputActionManifestAbsolutePath );
S_API void SteamAPI_ISteamInput_RunFrame( ISteamInput* self, bool bReservedValue );
S_API bool SteamAPI_ISteamInput_BWaitForData( ISteamInput* self, bool bWaitForever, uint32 unTimeout );
S_API bool SteamAPI_ISteamInput_BNewDataAvailable( ISteamInput* self );
S_API int SteamAPI_ISteamInput_GetConnectedControllers( ISteamInput* self, InputHandle_t * handlesOut );
S_API void SteamAPI_ISteamInput_EnableDeviceCallbacks( ISteamInput* self );
S_API void SteamAPI_ISteamInput_EnableActionEventCallbacks( ISteamInput* self, SteamInputActionEventCallbackPointer pCallback );
S_API InputActionSetHandle_t SteamAPI_ISteamInput_GetActionSetHandle( ISteamInput* self, const char * pszActionSetName );
S_API void SteamAPI_ISteamInput_ActivateActionSet( ISteamInput* self, InputHandle_t inputHandle, InputActionSetHandle_t actionSetHandle );
S_API InputActionSetHandle_t SteamAPI_ISteamInput_GetCurrentActionSet( ISteamInput* self, InputHandle_t inputHandle );
S_API void SteamAPI_ISteamInput_ActivateActionSetLayer( ISteamInput* self, InputHandle_t inputHandle, InputActionSetHandle_t actionSetLayerHandle );
S_API void SteamAPI_ISteamInput_DeactivateActionSetLayer( ISteamInput* self, InputHandle_t inputHandle, InputActionSetHandle_t actionSetLayerHandle );
S_API void SteamAPI_ISteamInput_DeactivateAllActionSetLayers( ISteamInput* self, InputHandle_t inputHandle );
S_API int SteamAPI_ISteamInput_GetActiveActionSetLayers( ISteamInput* self, InputHandle_t inputHandle, InputActionSetHandle_t * handlesOut );
S_API InputDigitalActionHandle_t SteamAPI_ISteamInput_GetDigitalActionHandle( ISteamInput* self, const char * pszActionName );
S_API InputDigitalActionData_t SteamAPI_ISteamInput_GetDigitalActionData( ISteamInput* self, InputHandle_t inputHandle, InputDigitalActionHandle_t digitalActionHandle );
S_API int SteamAPI_ISteamInput_GetDigitalActionOrigins( ISteamInput* self, InputHandle_t inputHandle, InputActionSetHandle_t actionSetHandle, InputDigitalActionHandle_t digitalActionHandle, EInputActionOrigin * originsOut );
S_API const char * SteamAPI_ISteamInput_GetStringForDigitalActionName( ISteamInput* self, InputDigitalActionHandle_t eActionHandle );
S_API InputAnalogActionHandle_t SteamAPI_ISteamInput_GetAnalogActionHandle( ISteamInput* self, const char * pszActionName );
S_API InputAnalogActionData_t SteamAPI_ISteamInput_GetAnalogActionData( ISteamInput* self, InputHandle_t inputHandle, InputAnalogActionHandle_t analogActionHandle );
S_API int SteamAPI_ISteamInput_GetAnalogActionOrigins( ISteamInput* self, InputHandle_t inputHandle, InputActionSetHandle_t actionSetHandle, InputAnalogActionHandle_t analogActionHandle, EInputActionOrigin * originsOut );
S_API const char * SteamAPI_ISteamInput_GetGlyphPNGForActionOrigin( ISteamInput* self, EInputActionOrigin eOrigin, ESteamInputGlyphSize eSize, uint32 unFlags );
S_API const char * SteamAPI_ISteamInput_GetGlyphSVGForActionOrigin( ISteamInput* self, EInputActionOrigin eOrigin, uint32 unFlags );
S_API const char * SteamAPI_ISteamInput_GetGlyphForActionOrigin_Legacy( ISteamInput* self, EInputActionOrigin eOrigin );
S_API const char * SteamAPI_ISteamInput_GetStringForActionOrigin( ISteamInput* self, EInputActionOrigin eOrigin );
S_API const char * SteamAPI_ISteamInput_GetStringForAnalogActionName( ISteamInput* self, InputAnalogActionHandle_t eActionHandle );
S_API void SteamAPI_ISteamInput_StopAnalogActionMomentum( ISteamInput* self, InputHandle_t inputHandle, InputAnalogActionHandle_t eAction );
S_API InputMotionData_t SteamAPI_ISteamInput_GetMotionData( ISteamInput* self, InputHandle_t inputHandle );
S_API void SteamAPI_ISteamInput_TriggerVibration( ISteamInput* self, InputHandle_t inputHandle, unsigned short usLeftSpeed, unsigned short usRightSpeed );
S_API void SteamAPI_ISteamInput_TriggerVibrationExtended( ISteamInput* self, InputHandle_t inputHandle, unsigned short usLeftSpeed, unsigned short usRightSpeed, unsigned short usLeftTriggerSpeed, unsigned short usRightTriggerSpeed );
S_API void SteamAPI_ISteamInput_TriggerSimpleHapticEvent( ISteamInput* self, InputHandle_t inputHandle, EControllerHapticLocation eHapticLocation, uint8 nIntensity, char nGainDB, uint8 nOtherIntensity, char nOtherGainDB );
S_API void SteamAPI_ISteamInput_SetLEDColor( ISteamInput* self, InputHandle_t inputHandle, uint8 nColorR, uint8 nColorG, uint8 nColorB, unsigned int nFlags );
S_API void SteamAPI_ISteamInput_Legacy_TriggerHapticPulse( ISteamInput* self, InputHandle_t inputHandle, ESteamControllerPad eTargetPad, unsigned short usDurationMicroSec );
S_API void SteamAPI_ISteamInput_Legacy_TriggerRepeatedHapticPulse( ISteamInput* self, InputHandle_t inputHandle, ESteamControllerPad eTargetPad, unsigned short usDurationMicroSec, unsigned short usOffMicroSec, unsigned short unRepeat, unsigned int nFlags );
S_API bool SteamAPI_ISteamInput_ShowBindingPanel( ISteamInput* self, InputHandle_t inputHandle );
S_API ESteamInputType SteamAPI_ISteamInput_GetInputTypeForHandle( ISteamInput* self, InputHandle_t inputHandle );
S_API InputHandle_t SteamAPI_ISteamInput_GetControllerForGamepadIndex( ISteamInput* self, int nIndex );
S_API int SteamAPI_ISteamInput_GetGamepadIndexForController( ISteamInput* self, InputHandle_t ulinputHandle );
S_API const char * SteamAPI_ISteamInput_GetStringForXboxOrigin( ISteamInput* self, EXboxOrigin eOrigin );
S_API const char * SteamAPI_ISteamInput_GetGlyphForXboxOrigin( ISteamInput* self, EXboxOrigin eOrigin );
S_API EInputActionOrigin SteamAPI_ISteamInput_GetActionOriginFromXboxOrigin( ISteamInput* self, InputHandle_t inputHandle, EXboxOrigin eOrigin );
S_API EInputActionOrigin SteamAPI_ISteamInput_TranslateActionOrigin( ISteamInput* self, ESteamInputType eDestinationInputType, EInputActionOrigin eSourceOrigin );
S_API bool SteamAPI_ISteamInput_GetDeviceBindingRevision( ISteamInput* self, InputHandle_t inputHandle, int * pMajor, int * pMinor );
S_API uint32 SteamAPI_ISteamInput_GetRemotePlaySessionID( ISteamInput* self, InputHandle_t inputHandle );
S_API uint16 SteamAPI_ISteamInput_GetSessionInputConfigurationSettings( ISteamInput* self );
S_API void SteamAPI_ISteamInput_SetDualSenseTriggerEffect( ISteamInput* self, InputHandle_t inputHandle, const ScePadTriggerEffectParam * pParam );

// ISteamController

// A versioned accessor is exported by the library
S_API ISteamController *SteamAPI_SteamController_v008();
// Inline, unversioned accessor to get the current version.  Essentially the same as SteamController(), but using this ensures that you are using a matching library.
inline ISteamController *SteamAPI_SteamController() { return SteamAPI_SteamController_v008(); }
S_API bool SteamAPI_ISteamController_Init( ISteamController* self );
S_API bool SteamAPI_ISteamController_Shutdown( ISteamController* self );
S_API void SteamAPI_ISteamController_RunFrame( ISteamController* self );
S_API int SteamAPI_ISteamController_GetConnectedControllers( ISteamController* self, ControllerHandle_t * handlesOut );
S_API ControllerActionSetHandle_t SteamAPI_ISteamController_GetActionSetHandle( ISteamController* self, const char * pszActionSetName );
S_API void SteamAPI_ISteamController_ActivateActionSet( ISteamController* self, ControllerHandle_t controllerHandle, ControllerActionSetHandle_t actionSetHandle );
S_API ControllerActionSetHandle_t SteamAPI_ISteamController_GetCurrentActionSet( ISteamController* self, ControllerHandle_t controllerHandle );
S_API void SteamAPI_ISteamController_ActivateActionSetLayer( ISteamController* self, ControllerHandle_t controllerHandle, ControllerActionSetHandle_t actionSetLayerHandle );
S_API void SteamAPI_ISteamController_DeactivateActionSetLayer( ISteamController* self, ControllerHandle_t controllerHandle, ControllerActionSetHandle_t actionSetLayerHandle );
S_API void SteamAPI_ISteamController_DeactivateAllActionSetLayers( ISteamController* self, ControllerHandle_t controllerHandle );
S_API int SteamAPI_ISteamController_GetActiveActionSetLayers( ISteamController* self, ControllerHandle_t controllerHandle, ControllerActionSetHandle_t * handlesOut );
S_API ControllerDigitalActionHandle_t SteamAPI_ISteamController_GetDigitalActionHandle( ISteamController* self, const char * pszActionName );
S_API InputDigitalActionData_t SteamAPI_ISteamController_GetDigitalActionData( ISteamController* self, ControllerHandle_t controllerHandle, ControllerDigitalActionHandle_t digitalActionHandle );
S_API int SteamAPI_ISteamController_GetDigitalActionOrigins( ISteamController* self, ControllerHandle_t controllerHandle, ControllerActionSetHandle_t actionSetHandle, ControllerDigitalActionHandle_t digitalActionHandle, EControllerActionOrigin * originsOut );
S_API ControllerAnalogActionHandle_t SteamAPI_ISteamController_GetAnalogActionHandle( ISteamController* self, const char * pszActionName );
S_API InputAnalogActionData_t SteamAPI_ISteamController_GetAnalogActionData( ISteamController* self, ControllerHandle_t controllerHandle, ControllerAnalogActionHandle_t analogActionHandle );
S_API int SteamAPI_ISteamController_GetAnalogActionOrigins( ISteamController* self, ControllerHandle_t controllerHandle, ControllerActionSetHandle_t actionSetHandle, ControllerAnalogActionHandle_t analogActionHandle, EControllerActionOrigin * originsOut );
S_API const char * SteamAPI_ISteamController_GetGlyphForActionOrigin( ISteamController* self, EControllerActionOrigin eOrigin );
S_API const char * SteamAPI_ISteamController_GetStringForActionOrigin( ISteamController* self, EControllerActionOrigin eOrigin );
S_API void SteamAPI_ISteamController_StopAnalogActionMomentum( ISteamController* self, ControllerHandle_t controllerHandle, ControllerAnalogActionHandle_t eAction );
S_API InputMotionData_t SteamAPI_ISteamController_GetMotionData( ISteamController* self, ControllerHandle_t controllerHandle );
S_API void SteamAPI_ISteamController_TriggerHapticPulse( ISteamController* self, ControllerHandle_t controllerHandle, ESteamControllerPad eTargetPad, unsigned short usDurationMicroSec );
S_API void SteamAPI_ISteamController_TriggerRepeatedHapticPulse( ISteamController* self, ControllerHandle_t controllerHandle, ESteamControllerPad eTargetPad, unsigned short usDurationMicroSec, unsigned short usOffMicroSec, unsigned short unRepeat, unsigned int nFlags );
S_API void SteamAPI_ISteamController_TriggerVibration( ISteamController* self, ControllerHandle_t controllerHandle, unsigned short usLeftSpeed, unsigned short usRightSpeed );
S_API void SteamAPI_ISteamController_SetLEDColor( ISteamController* self, ControllerHandle_t controllerHandle, uint8 nColorR, uint8 nColorG, uint8 nColorB, unsigned int nFlags );
S_API bool SteamAPI_ISteamController_ShowBindingPanel( ISteamController* self, ControllerHandle_t controllerHandle );
S_API ESteamInputType SteamAPI_ISteamController_GetInputTypeForHandle( ISteamController* self, ControllerHandle_t controllerHandle );
S_API ControllerHandle_t SteamAPI_ISteamController_GetControllerForGamepadIndex( ISteamController* self, int nIndex );
S_API int SteamAPI_ISteamController_GetGamepadIndexForController( ISteamController* self, ControllerHandle_t ulControllerHandle );
S_API const char * SteamAPI_ISteamController_GetStringForXboxOrigin( ISteamController* self, EXboxOrigin eOrigin );
S_API const char * SteamAPI_ISteamController_GetGlyphForXboxOrigin( ISteamController* self, EXboxOrigin eOrigin );
S_API EControllerActionOrigin SteamAPI_ISteamController_GetActionOriginFromXboxOrigin( ISteamController* self, ControllerHandle_t controllerHandle, EXboxOrigin eOrigin );
S_API EControllerActionOrigin SteamAPI_ISteamController_TranslateActionOrigin( ISteamController* self, ESteamInputType eDestinationInputType, EControllerActionOrigin eSourceOrigin );
S_API bool SteamAPI_ISteamController_GetControllerBindingRevision( ISteamController* self, ControllerHandle_t controllerHandle, int * pMajor, int * pMinor );

// ISteamUGC

// A versioned accessor is exported by the library
S_API ISteamUGC *SteamAPI_SteamUGC_v020();
// Inline, unversioned accessor to get the current version.  Essentially the same as SteamUGC(), but using this ensures that you are using a matching library.
inline ISteamUGC *SteamAPI_SteamUGC() { return SteamAPI_SteamUGC_v020(); }

// A versioned accessor is exported by the library
S_API ISteamUGC *SteamAPI_SteamGameServerUGC_v020();
// Inline, unversioned accessor to get the current version.  Essentially the same as SteamGameServerUGC(), but using this ensures that you are using a matching library.
inline ISteamUGC *SteamAPI_SteamGameServerUGC() { return SteamAPI_SteamGameServerUGC_v020(); }
S_API UGCQueryHandle_t SteamAPI_ISteamUGC_CreateQueryUserUGCRequest( ISteamUGC* self, AccountID_t unAccountID, EUserUGCList eListType, EUGCMatchingUGCType eMatchingUGCType, EUserUGCListSortOrder eSortOrder, AppId_t nCreatorAppID, AppId_t nConsumerAppID, uint32 unPage );
S_API UGCQueryHandle_t SteamAPI_ISteamUGC_CreateQueryAllUGCRequestPage( ISteamUGC* self, EUGCQuery eQueryType, EUGCMatchingUGCType eMatchingeMatchingUGCTypeFileType, AppId_t nCreatorAppID, AppId_t nConsumerAppID, uint32 unPage );
S_API UGCQueryHandle_t SteamAPI_ISteamUGC_CreateQueryAllUGCRequestCursor( ISteamUGC* self, EUGCQuery eQueryType, EUGCMatchingUGCType eMatchingeMatchingUGCTypeFileType, AppId_t nCreatorAppID, AppId_t nConsumerAppID, const char * pchCursor );
S_API UGCQueryHandle_t SteamAPI_ISteamUGC_CreateQueryUGCDetailsRequest( ISteamUGC* self, PublishedFileId_t * pvecPublishedFileID, uint32 unNumPublishedFileIDs );
S_API SteamAPICall_t SteamAPI_ISteamUGC_SendQueryUGCRequest( ISteamUGC* self, UGCQueryHandle_t handle );
S_API bool SteamAPI_ISteamUGC_GetQueryUGCResult( ISteamUGC* self, UGCQueryHandle_t handle, uint32 index, SteamUGCDetails_t * pDetails );
S_API uint32 SteamAPI_ISteamUGC_GetQueryUGCNumTags( ISteamUGC* self, UGCQueryHandle_t handle, uint32 index );
S_API bool SteamAPI_ISteamUGC_GetQueryUGCTag( ISteamUGC* self, UGCQueryHandle_t handle, uint32 index, uint32 indexTag, char * pchValue, uint32 cchValueSize );
S_API bool SteamAPI_ISteamUGC_GetQueryUGCTagDisplayName( ISteamUGC* self, UGCQueryHandle_t handle, uint32 index, uint32 indexTag, char * pchValue, uint32 cchValueSize );
S_API bool SteamAPI_ISteamUGC_GetQueryUGCPreviewURL( ISteamUGC* self, UGCQueryHandle_t handle, uint32 index, char * pchURL, uint32 cchURLSize );
S_API bool SteamAPI_ISteamUGC_GetQueryUGCMetadata( ISteamUGC* self, UGCQueryHandle_t handle, uint32 index, char * pchMetadata, uint32 cchMetadatasize );
S_API bool SteamAPI_ISteamUGC_GetQueryUGCChildren( ISteamUGC* self, UGCQueryHandle_t handle, uint32 index, PublishedFileId_t * pvecPublishedFileID, uint32 cMaxEntries );
S_API bool SteamAPI_ISteamUGC_GetQueryUGCStatistic( ISteamUGC* self, UGCQueryHandle_t handle, uint32 index, EItemStatistic eStatType, uint64 * pStatValue );
S_API uint32 SteamAPI_ISteamUGC_GetQueryUGCNumAdditionalPreviews( ISteamUGC* self, UGCQueryHandle_t handle, uint32 index );
S_API bool SteamAPI_ISteamUGC_GetQueryUGCAdditionalPreview( ISteamUGC* self, UGCQueryHandle_t handle, uint32 index, uint32 previewIndex, char * pchURLOrVideoID, uint32 cchURLSize, char * pchOriginalFileName, uint32 cchOriginalFileNameSize, EItemPreviewType * pPreviewType );
S_API uint32 SteamAPI_ISteamUGC_GetQueryUGCNumKeyValueTags( ISteamUGC* self, UGCQueryHandle_t handle, uint32 index );
S_API bool SteamAPI_ISteamUGC_GetQueryUGCKeyValueTag( ISteamUGC* self, UGCQueryHandle_t handle, uint32 index, uint32 keyValueTagIndex, char * pchKey, uint32 cchKeySize, char * pchValue, uint32 cchValueSize );
S_API bool SteamAPI_ISteamUGC_GetQueryFirstUGCKeyValueTag( ISteamUGC* self, UGCQueryHandle_t handle, uint32 index, const char * pchKey, char * pchValue, uint32 cchValueSize );
S_API uint32 SteamAPI_ISteamUGC_GetNumSupportedGameVersions( ISteamUGC* self, UGCQueryHandle_t handle, uint32 index );
S_API bool SteamAPI_ISteamUGC_GetSupportedGameVersionData( ISteamUGC* self, UGCQueryHandle_t handle, uint32 index, uint32 versionIndex, char * pchGameBranchMin, char * pchGameBranchMax, uint32 cchGameBranchSize );
S_API uint32 SteamAPI_ISteamUGC_GetQueryUGCContentDescriptors( ISteamUGC* self, UGCQueryHandle_t handle, uint32 index, EUGCContentDescriptorID * pvecDescriptors, uint32 cMaxEntries );
S_API bool SteamAPI_ISteamUGC_ReleaseQueryUGCRequest( ISteamUGC* self, UGCQueryHandle_t handle );
S_API bool SteamAPI_ISteamUGC_AddRequiredTag( ISteamUGC* self, UGCQueryHandle_t handle, const char * pTagName );
S_API bool SteamAPI_ISteamUGC_AddRequiredTagGroup( ISteamUGC* self, UGCQueryHandle_t handle, const SteamParamStringArray_t * pTagGroups );
S_API bool SteamAPI_ISteamUGC_AddExcludedTag( ISteamUGC* self, UGCQueryHandle_t handle, const char * pTagName );
S_API bool SteamAPI_ISteamUGC_SetReturnOnlyIDs( ISteamUGC* self, UGCQueryHandle_t handle, bool bReturnOnlyIDs );
S_API bool SteamAPI_ISteamUGC_SetReturnKeyValueTags( ISteamUGC* self, UGCQueryHandle_t handle, bool bReturnKeyValueTags );
S_API bool SteamAPI_ISteamUGC_SetReturnLongDescription( ISteamUGC* self, UGCQueryHandle_t handle, bool bReturnLongDescription );
S_API bool SteamAPI_ISteamUGC_SetReturnMetadata( ISteamUGC* self, UGCQueryHandle_t handle, bool bReturnMetadata );
S_API bool SteamAPI_ISteamUGC_SetReturnChildren( ISteamUGC* self, UGCQueryHandle_t handle, bool bReturnChildren );
S_API bool SteamAPI_ISteamUGC_SetReturnAdditionalPreviews( ISteamUGC* self, UGCQueryHandle_t handle, bool bReturnAdditionalPreviews );
S_API bool SteamAPI_ISteamUGC_SetReturnTotalOnly( ISteamUGC* self, UGCQueryHandle_t handle, bool bReturnTotalOnly );
S_API bool SteamAPI_ISteamUGC_SetReturnPlaytimeStats( ISteamUGC* self, UGCQueryHandle_t handle, uint32 unDays );
S_API bool SteamAPI_ISteamUGC_SetLanguage( ISteamUGC* self, UGCQueryHandle_t handle, const char * pchLanguage );
S_API bool SteamAPI_ISteamUGC_SetAllowCachedResponse( ISteamUGC* self, UGCQueryHandle_t handle, uint32 unMaxAgeSeconds );
S_API bool SteamAPI_ISteamUGC_SetAdminQuery( ISteamUGC* self, UGCUpdateHandle_t handle, bool bAdminQuery );
S_API bool SteamAPI_ISteamUGC_SetCloudFileNameFilter( ISteamUGC* self, UGCQueryHandle_t handle, const char * pMatchCloudFileName );
S_API bool SteamAPI_ISteamUGC_SetMatchAnyTag( ISteamUGC* self, UGCQueryHandle_t handle, bool bMatchAnyTag );
S_API bool SteamAPI_ISteamUGC_SetSearchText( ISteamUGC* self, UGCQueryHandle_t handle, const char * pSearchText );
S_API bool SteamAPI_ISteamUGC_SetRankedByTrendDays( ISteamUGC* self, UGCQueryHandle_t handle, uint32 unDays );
S_API bool SteamAPI_ISteamUGC_SetTimeCreatedDateRange( ISteamUGC* self, UGCQueryHandle_t handle, RTime32 rtStart, RTime32 rtEnd );
S_API bool SteamAPI_ISteamUGC_SetTimeUpdatedDateRange( ISteamUGC* self, UGCQueryHandle_t handle, RTime32 rtStart, RTime32 rtEnd );
S_API bool SteamAPI_ISteamUGC_AddRequiredKeyValueTag( ISteamUGC* self, UGCQueryHandle_t handle, const char * pKey, const char * pValue );
S_API SteamAPICall_t SteamAPI_ISteamUGC_RequestUGCDetails( ISteamUGC* self, PublishedFileId_t nPublishedFileID, uint32 unMaxAgeSeconds );
S_API SteamAPICall_t SteamAPI_ISteamUGC_CreateItem( ISteamUGC* self, AppId_t nConsumerAppId, EWorkshopFileType eFileType );
S_API UGCUpdateHandle_t SteamAPI_ISteamUGC_StartItemUpdate( ISteamUGC* self, AppId_t nConsumerAppId, PublishedFileId_t nPublishedFileID );
S_API bool SteamAPI_ISteamUGC_SetItemTitle( ISteamUGC* self, UGCUpdateHandle_t handle, const char * pchTitle );
S_API bool SteamAPI_ISteamUGC_SetItemDescription( ISteamUGC* self, UGCUpdateHandle_t handle, const char * pchDescription );
S_API bool SteamAPI_ISteamUGC_SetItemUpdateLanguage( ISteamUGC* self, UGCUpdateHandle_t handle, const char * pchLanguage );
S_API bool SteamAPI_ISteamUGC_SetItemMetadata( ISteamUGC* self, UGCUpdateHandle_t handle, const char * pchMetaData );
S_API bool SteamAPI_ISteamUGC_SetItemVisibility( ISteamUGC* self, UGCUpdateHandle_t handle, ERemoteStoragePublishedFileVisibility eVisibility );
S_API bool SteamAPI_ISteamUGC_SetItemTags( ISteamUGC* self, UGCUpdateHandle_t updateHandle, const SteamParamStringArray_t * pTags, bool bAllowAdminTags );
S_API bool SteamAPI_ISteamUGC_SetItemContent( ISteamUGC* self, UGCUpdateHandle_t handle, const char * pszContentFolder );
S_API bool SteamAPI_ISteamUGC_SetItemPreview( ISteamUGC* self, UGCUpdateHandle_t handle, const char * pszPreviewFile );
S_API bool SteamAPI_ISteamUGC_SetAllowLegacyUpload( ISteamUGC* self, UGCUpdateHandle_t handle, bool bAllowLegacyUpload );
S_API bool SteamAPI_ISteamUGC_RemoveAllItemKeyValueTags( ISteamUGC* self, UGCUpdateHandle_t handle );
S_API bool SteamAPI_ISteamUGC_RemoveItemKeyValueTags( ISteamUGC* self, UGCUpdateHandle_t handle, const char * pchKey );
S_API bool SteamAPI_ISteamUGC_AddItemKeyValueTag( ISteamUGC* self, UGCUpdateHandle_t handle, const char * pchKey, const char * pchValue );
S_API bool SteamAPI_ISteamUGC_AddItemPreviewFile( ISteamUGC* self, UGCUpdateHandle_t handle, const char * pszPreviewFile, EItemPreviewType type );
S_API bool SteamAPI_ISteamUGC_AddItemPreviewVideo( ISteamUGC* self, UGCUpdateHandle_t handle, const char * pszVideoID );
S_API bool SteamAPI_ISteamUGC_UpdateItemPreviewFile( ISteamUGC* self, UGCUpdateHandle_t handle, uint32 index, const char * pszPreviewFile );
S_API bool SteamAPI_ISteamUGC_UpdateItemPreviewVideo( ISteamUGC* self, UGCUpdateHandle_t handle, uint32 index, const char * pszVideoID );
S_API bool SteamAPI_ISteamUGC_RemoveItemPreview( ISteamUGC* self, UGCUpdateHandle_t handle, uint32 index );
S_API bool SteamAPI_ISteamUGC_AddContentDescriptor( ISteamUGC* self, UGCUpdateHandle_t handle, EUGCContentDescriptorID descid );
S_API bool SteamAPI_ISteamUGC_RemoveContentDescriptor( ISteamUGC* self, UGCUpdateHandle_t handle, EUGCContentDescriptorID descid );
S_API bool SteamAPI_ISteamUGC_SetRequiredGameVersions( ISteamUGC* self, UGCUpdateHandle_t handle, const char * pszGameBranchMin, const char * pszGameBranchMax );
S_API SteamAPICall_t SteamAPI_ISteamUGC_SubmitItemUpdate( ISteamUGC* self, UGCUpdateHandle_t handle, const char * pchChangeNote );
S_API EItemUpdateStatus SteamAPI_ISteamUGC_GetItemUpdateProgress( ISteamUGC* self, UGCUpdateHandle_t handle, uint64 * punBytesProcessed, uint64 * punBytesTotal );
S_API SteamAPICall_t SteamAPI_ISteamUGC_SetUserItemVote( ISteamUGC* self, PublishedFileId_t nPublishedFileID, bool bVoteUp );
S_API SteamAPICall_t SteamAPI_ISteamUGC_GetUserItemVote( ISteamUGC* self, PublishedFileId_t nPublishedFileID );
S_API SteamAPICall_t SteamAPI_ISteamUGC_AddItemToFavorites( ISteamUGC* self, AppId_t nAppId, PublishedFileId_t nPublishedFileID );
S_API SteamAPICall_t SteamAPI_ISteamUGC_RemoveItemFromFavorites( ISteamUGC* self, AppId_t nAppId, PublishedFileId_t nPublishedFileID );
S_API SteamAPICall_t SteamAPI_ISteamUGC_SubscribeItem( ISteamUGC* self, PublishedFileId_t nPublishedFileID );
S_API SteamAPICall_t SteamAPI_ISteamUGC_UnsubscribeItem( ISteamUGC* self, PublishedFileId_t nPublishedFileID );
S_API uint32 SteamAPI_ISteamUGC_GetNumSubscribedItems( ISteamUGC* self );
S_API uint32 SteamAPI_ISteamUGC_GetSubscribedItems( ISteamUGC* self, PublishedFileId_t * pvecPublishedFileID, uint32 cMaxEntries );
S_API uint32 SteamAPI_ISteamUGC_GetItemState( ISteamUGC* self, PublishedFileId_t nPublishedFileID );
S_API bool SteamAPI_ISteamUGC_GetItemInstallInfo( ISteamUGC* self, PublishedFileId_t nPublishedFileID, uint64 * punSizeOnDisk, char * pchFolder, uint32 cchFolderSize, uint32 * punTimeStamp );
S_API bool SteamAPI_ISteamUGC_GetItemDownloadInfo( ISteamUGC* self, PublishedFileId_t nPublishedFileID, uint64 * punBytesDownloaded, uint64 * punBytesTotal );
S_API bool SteamAPI_ISteamUGC_DownloadItem( ISteamUGC* self, PublishedFileId_t nPublishedFileID, bool bHighPriority );
S_API bool SteamAPI_ISteamUGC_BInitWorkshopForGameServer( ISteamUGC* self, DepotId_t unWorkshopDepotID, const char * pszFolder );
S_API void SteamAPI_ISteamUGC_SuspendDownloads( ISteamUGC* self, bool bSuspend );
S_API SteamAPICall_t SteamAPI_ISteamUGC_StartPlaytimeTracking( ISteamUGC* self, PublishedFileId_t * pvecPublishedFileID, uint32 unNumPublishedFileIDs );
S_API SteamAPICall_t SteamAPI_ISteamUGC_StopPlaytimeTracking( ISteamUGC* self, PublishedFileId_t * pvecPublishedFileID, uint32 unNumPublishedFileIDs );
S_API SteamAPICall_t SteamAPI_ISteamUGC_StopPlaytimeTrackingForAllItems( ISteamUGC* self );
S_API SteamAPICall_t SteamAPI_ISteamUGC_AddDependency( ISteamUGC* self, PublishedFileId_t nParentPublishedFileID, PublishedFileId_t nChildPublishedFileID );
S_API SteamAPICall_t SteamAPI_ISteamUGC_RemoveDependency( ISteamUGC* self, PublishedFileId_t nParentPublishedFileID, PublishedFileId_t nChildPublishedFileID );
S_API SteamAPICall_t SteamAPI_ISteamUGC_AddAppDependency( ISteamUGC* self, PublishedFileId_t nPublishedFileID, AppId_t nAppID );
S_API SteamAPICall_t SteamAPI_ISteamUGC_RemoveAppDependency( ISteamUGC* self, PublishedFileId_t nPublishedFileID, AppId_t nAppID );
S_API SteamAPICall_t SteamAPI_ISteamUGC_GetAppDependencies( ISteamUGC* self, PublishedFileId_t nPublishedFileID );
S_API SteamAPICall_t SteamAPI_ISteamUGC_DeleteItem( ISteamUGC* self, PublishedFileId_t nPublishedFileID );
S_API bool SteamAPI_ISteamUGC_ShowWorkshopEULA( ISteamUGC* self );
S_API SteamAPICall_t SteamAPI_ISteamUGC_GetWorkshopEULAStatus( ISteamUGC* self );
S_API uint32 SteamAPI_ISteamUGC_GetUserContentDescriptorPreferences( ISteamUGC* self, EUGCContentDescriptorID * pvecDescriptors, uint32 cMaxEntries );

// ISteamHTMLSurface

// A versioned accessor is exported by the library
S_API ISteamHTMLSurface *SteamAPI_SteamHTMLSurface_v005();
// Inline, unversioned accessor to get the current version.  Essentially the same as SteamHTMLSurface(), but using this ensures that you are using a matching library.
inline ISteamHTMLSurface *SteamAPI_SteamHTMLSurface() { return SteamAPI_SteamHTMLSurface_v005(); }
S_API bool SteamAPI_ISteamHTMLSurface_Init( ISteamHTMLSurface* self );
S_API bool SteamAPI_ISteamHTMLSurface_Shutdown( ISteamHTMLSurface* self );
S_API SteamAPICall_t SteamAPI_ISteamHTMLSurface_CreateBrowser( ISteamHTMLSurface* self, const char * pchUserAgent, const char * pchUserCSS );
S_API void SteamAPI_ISteamHTMLSurface_RemoveBrowser( ISteamHTMLSurface* self, HHTMLBrowser unBrowserHandle );
S_API void SteamAPI_ISteamHTMLSurface_LoadURL( ISteamHTMLSurface* self, HHTMLBrowser unBrowserHandle, const char * pchURL, const char * pchPostData );
S_API void SteamAPI_ISteamHTMLSurface_SetSize( ISteamHTMLSurface* self, HHTMLBrowser unBrowserHandle, uint32 unWidth, uint32 unHeight );
S_API void SteamAPI_ISteamHTMLSurface_StopLoad( ISteamHTMLSurface* self, HHTMLBrowser unBrowserHandle );
S_API void SteamAPI_ISteamHTMLSurface_Reload( ISteamHTMLSurface* self, HHTMLBrowser unBrowserHandle );
S_API void SteamAPI_ISteamHTMLSurface_GoBack( ISteamHTMLSurface* self, HHTMLBrowser unBrowserHandle );
S_API void SteamAPI_ISteamHTMLSurface_GoForward( ISteamHTMLSurface* self, HHTMLBrowser unBrowserHandle );
S_API void SteamAPI_ISteamHTMLSurface_AddHeader( ISteamHTMLSurface* self, HHTMLBrowser unBrowserHandle, const char * pchKey, const char * pchValue );
S_API void SteamAPI_ISteamHTMLSurface_ExecuteJavascript( ISteamHTMLSurface* self, HHTMLBrowser unBrowserHandle, const char * pchScript );
S_API void SteamAPI_ISteamHTMLSurface_MouseUp( ISteamHTMLSurface* self, HHTMLBrowser unBrowserHandle, EHTMLMouseButton eMouseButton );
S_API void SteamAPI_ISteamHTMLSurface_MouseDown( ISteamHTMLSurface* self, HHTMLBrowser unBrowserHandle, EHTMLMouseButton eMouseButton );
S_API void SteamAPI_ISteamHTMLSurface_MouseDoubleClick( ISteamHTMLSurface* self, HHTMLBrowser unBrowserHandle, EHTMLMouseButton eMouseButton );
S_API void SteamAPI_ISteamHTMLSurface_MouseMove( ISteamHTMLSurface* self, HHTMLBrowser unBrowserHandle, int x, int y );
S_API void SteamAPI_ISteamHTMLSurface_MouseWheel( ISteamHTMLSurface* self, HHTMLBrowser unBrowserHandle, int32 nDelta );
S_API void SteamAPI_ISteamHTMLSurface_KeyDown( ISteamHTMLSurface* self, HHTMLBrowser unBrowserHandle, uint32 nNativeKeyCode, EHTMLKeyModifiers eHTMLKeyModifiers, bool bIsSystemKey );
S_API void SteamAPI_ISteamHTMLSurface_KeyUp( ISteamHTMLSurface* self, HHTMLBrowser unBrowserHandle, uint32 nNativeKeyCode, EHTMLKeyModifiers eHTMLKeyModifiers );
S_API void SteamAPI_ISteamHTMLSurface_KeyChar( ISteamHTMLSurface* self, HHTMLBrowser unBrowserHandle, uint32 cUnicodeChar, EHTMLKeyModifiers eHTMLKeyModifiers );
S_API void SteamAPI_ISteamHTMLSurface_SetHorizontalScroll( ISteamHTMLSurface* self, HHTMLBrowser unBrowserHandle, uint32 nAbsolutePixelScroll );
S_API void SteamAPI_ISteamHTMLSurface_SetVerticalScroll( ISteamHTMLSurface* self, HHTMLBrowser unBrowserHandle, uint32 nAbsolutePixelScroll );
S_API void SteamAPI_ISteamHTMLSurface_SetKeyFocus( ISteamHTMLSurface* self, HHTMLBrowser unBrowserHandle, bool bHasKeyFocus );
S_API void SteamAPI_ISteamHTMLSurface_ViewSource( ISteamHTMLSurface* self, HHTMLBrowser unBrowserHandle );
S_API void SteamAPI_ISteamHTMLSurface_CopyToClipboard( ISteamHTMLSurface* self, HHTMLBrowser unBrowserHandle );
S_API void SteamAPI_ISteamHTMLSurface_PasteFromClipboard( ISteamHTMLSurface* self, HHTMLBrowser unBrowserHandle );
S_API void SteamAPI_ISteamHTMLSurface_Find( ISteamHTMLSurface* self, HHTMLBrowser unBrowserHandle, const char * pchSearchStr, bool bCurrentlyInFind, bool bReverse );
S_API void SteamAPI_ISteamHTMLSurface_StopFind( ISteamHTMLSurface* self, HHTMLBrowser unBrowserHandle );
S_API void SteamAPI_ISteamHTMLSurface_GetLinkAtPosition( ISteamHTMLSurface* self, HHTMLBrowser unBrowserHandle, int x, int y );
S_API void SteamAPI_ISteamHTMLSurface_SetCookie( ISteamHTMLSurface* self, const char * pchHostname, const char * pchKey, const char * pchValue, const char * pchPath, RTime32 nExpires, bool bSecure, bool bHTTPOnly );
S_API void SteamAPI_ISteamHTMLSurface_SetPageScaleFactor( ISteamHTMLSurface* self, HHTMLBrowser unBrowserHandle, float flZoom, int nPointX, int nPointY );
S_API void SteamAPI_ISteamHTMLSurface_SetBackgroundMode( ISteamHTMLSurface* self, HHTMLBrowser unBrowserHandle, bool bBackgroundMode );
S_API void SteamAPI_ISteamHTMLSurface_SetDPIScalingFactor( ISteamHTMLSurface* self, HHTMLBrowser unBrowserHandle, float flDPIScaling );
S_API void SteamAPI_ISteamHTMLSurface_OpenDeveloperTools( ISteamHTMLSurface* self, HHTMLBrowser unBrowserHandle );
S_API void SteamAPI_ISteamHTMLSurface_AllowStartRequest( ISteamHTMLSurface* self, HHTMLBrowser unBrowserHandle, bool bAllowed );
S_API void SteamAPI_ISteamHTMLSurface_JSDialogResponse( ISteamHTMLSurface* self, HHTMLBrowser unBrowserHandle, bool bResult );
S_API void SteamAPI_ISteamHTMLSurface_FileLoadDialogResponse( ISteamHTMLSurface* self, HHTMLBrowser unBrowserHandle, const char ** pchSelectedFiles );

// ISteamInventory

// A versioned accessor is exported by the library
S_API ISteamInventory *SteamAPI_SteamInventory_v003();
// Inline, unversioned accessor to get the current version.  Essentially the same as SteamInventory(), but using this ensures that you are using a matching library.
inline ISteamInventory *SteamAPI_SteamInventory() { return SteamAPI_SteamInventory_v003(); }

// A versioned accessor is exported by the library
S_API ISteamInventory *SteamAPI_SteamGameServerInventory_v003();
// Inline, unversioned accessor to get the current version.  Essentially the same as SteamGameServerInventory(), but using this ensures that you are using a matching library.
inline ISteamInventory *SteamAPI_SteamGameServerInventory() { return SteamAPI_SteamGameServerInventory_v003(); }
S_API EResult SteamAPI_ISteamInventory_GetResultStatus( ISteamInventory* self, SteamInventoryResult_t resultHandle );
S_API bool SteamAPI_ISteamInventory_GetResultItems( ISteamInventory* self, SteamInventoryResult_t resultHandle, SteamItemDetails_t * pOutItemsArray, uint32 * punOutItemsArraySize );
S_API bool SteamAPI_ISteamInventory_GetResultItemProperty( ISteamInventory* self, SteamInventoryResult_t resultHandle, uint32 unItemIndex, const char * pchPropertyName, char * pchValueBuffer, uint32 * punValueBufferSizeOut );
S_API uint32 SteamAPI_ISteamInventory_GetResultTimestamp( ISteamInventory* self, SteamInventoryResult_t resultHandle );
S_API bool SteamAPI_ISteamInventory_CheckResultSteamID( ISteamInventory* self, SteamInventoryResult_t resultHandle, uint64_steamid steamIDExpected );
S_API void SteamAPI_ISteamInventory_DestroyResult( ISteamInventory* self, SteamInventoryResult_t resultHandle );
S_API bool SteamAPI_ISteamInventory_GetAllItems( ISteamInventory* self, SteamInventoryResult_t * pResultHandle );
S_API bool SteamAPI_ISteamInventory_GetItemsByID( ISteamInventory* self, SteamInventoryResult_t * pResultHandle, const SteamItemInstanceID_t * pInstanceIDs, uint32 unCountInstanceIDs );
S_API bool SteamAPI_ISteamInventory_SerializeResult( ISteamInventory* self, SteamInventoryResult_t resultHandle, void * pOutBuffer, uint32 * punOutBufferSize );
S_API bool SteamAPI_ISteamInventory_DeserializeResult( ISteamInventory* self, SteamInventoryResult_t * pOutResultHandle, const void * pBuffer, uint32 unBufferSize, bool bRESERVED_MUST_BE_FALSE );
S_API bool SteamAPI_ISteamInventory_GenerateItems( ISteamInventory* self, SteamInventoryResult_t * pResultHandle, const SteamItemDef_t * pArrayItemDefs, const uint32 * punArrayQuantity, uint32 unArrayLength );
S_API bool SteamAPI_ISteamInventory_GrantPromoItems( ISteamInventory* self, SteamInventoryResult_t * pResultHandle );
S_API bool SteamAPI_ISteamInventory_AddPromoItem( ISteamInventory* self, SteamInventoryResult_t * pResultHandle, SteamItemDef_t itemDef );
S_API bool SteamAPI_ISteamInventory_AddPromoItems( ISteamInventory* self, SteamInventoryResult_t * pResultHandle, const SteamItemDef_t * pArrayItemDefs, uint32 unArrayLength );
S_API bool SteamAPI_ISteamInventory_ConsumeItem( ISteamInventory* self, SteamInventoryResult_t * pResultHandle, SteamItemInstanceID_t itemConsume, uint32 unQuantity );
S_API bool SteamAPI_ISteamInventory_ExchangeItems( ISteamInventory* self, SteamInventoryResult_t * pResultHandle, const SteamItemDef_t * pArrayGenerate, const uint32 * punArrayGenerateQuantity, uint32 unArrayGenerateLength, const SteamItemInstanceID_t * pArrayDestroy, const uint32 * punArrayDestroyQuantity, uint32 unArrayDestroyLength );
S_API bool SteamAPI_ISteamInventory_TransferItemQuantity( ISteamInventory* self, SteamInventoryResult_t * pResultHandle, SteamItemInstanceID_t itemIdSource, uint32 unQuantity, SteamItemInstanceID_t itemIdDest );
S_API void SteamAPI_ISteamInventory_SendItemDropHeartbeat( ISteamInventory* self );
S_API bool SteamAPI_ISteamInventory_TriggerItemDrop( ISteamInventory* self, SteamInventoryResult_t * pResultHandle, SteamItemDef_t dropListDefinition );
S_API bool SteamAPI_ISteamInventory_TradeItems( ISteamInventory* self, SteamInventoryResult_t * pResultHandle, uint64_steamid steamIDTradePartner, const SteamItemInstanceID_t * pArrayGive, const uint32 * pArrayGiveQuantity, uint32 nArrayGiveLength, const SteamItemInstanceID_t * pArrayGet, const uint32 * pArrayGetQuantity, uint32 nArrayGetLength );
S_API bool SteamAPI_ISteamInventory_LoadItemDefinitions( ISteamInventory* self );
S_API bool SteamAPI_ISteamInventory_GetItemDefinitionIDs( ISteamInventory* self, SteamItemDef_t * pItemDefIDs, uint32 * punItemDefIDsArraySize );
S_API bool SteamAPI_ISteamInventory_GetItemDefinitionProperty( ISteamInventory* self, SteamItemDef_t iDefinition, const char * pchPropertyName, char * pchValueBuffer, uint32 * punValueBufferSizeOut );
S_API SteamAPICall_t SteamAPI_ISteamInventory_RequestEligiblePromoItemDefinitionsIDs( ISteamInventory* self, uint64_steamid steamID );
S_API bool SteamAPI_ISteamInventory_GetEligiblePromoItemDefinitionIDs( ISteamInventory* self, uint64_steamid steamID, SteamItemDef_t * pItemDefIDs, uint32 * punItemDefIDsArraySize );
S_API SteamAPICall_t SteamAPI_ISteamInventory_StartPurchase( ISteamInventory* self, const SteamItemDef_t * pArrayItemDefs, const uint32 * punArrayQuantity, uint32 unArrayLength );
S_API SteamAPICall_t SteamAPI_ISteamInventory_RequestPrices( ISteamInventory* self );
S_API uint32 SteamAPI_ISteamInventory_GetNumItemsWithPrices( ISteamInventory* self );
S_API bool SteamAPI_ISteamInventory_GetItemsWithPrices( ISteamInventory* self, SteamItemDef_t * pArrayItemDefs, uint64 * pCurrentPrices, uint64 * pBasePrices, uint32 unArrayLength );
S_API bool SteamAPI_ISteamInventory_GetItemPrice( ISteamInventory* self, SteamItemDef_t iDefinition, uint64 * pCurrentPrice, uint64 * pBasePrice );
S_API SteamInventoryUpdateHandle_t SteamAPI_ISteamInventory_StartUpdateProperties( ISteamInventory* self );
S_API bool SteamAPI_ISteamInventory_RemoveProperty( ISteamInventory* self, SteamInventoryUpdateHandle_t handle, SteamItemInstanceID_t nItemID, const char * pchPropertyName );
S_API bool SteamAPI_ISteamInventory_SetPropertyString( ISteamInventory* self, SteamInventoryUpdateHandle_t handle, SteamItemInstanceID_t nItemID, const char * pchPropertyName, const char * pchPropertyValue );
S_API bool SteamAPI_ISteamInventory_SetPropertyBool( ISteamInventory* self, SteamInventoryUpdateHandle_t handle, SteamItemInstanceID_t nItemID, const char * pchPropertyName, bool bValue );
S_API bool SteamAPI_ISteamInventory_SetPropertyInt64( ISteamInventory* self, SteamInventoryUpdateHandle_t handle, SteamItemInstanceID_t nItemID, const char * pchPropertyName, int64 nValue );
S_API bool SteamAPI_ISteamInventory_SetPropertyFloat( ISteamInventory* self, SteamInventoryUpdateHandle_t handle, SteamItemInstanceID_t nItemID, const char * pchPropertyName, float flValue );
S_API bool SteamAPI_ISteamInventory_SubmitUpdateProperties( ISteamInventory* self, SteamInventoryUpdateHandle_t handle, SteamInventoryResult_t * pResultHandle );
S_API bool SteamAPI_ISteamInventory_InspectItem( ISteamInventory* self, SteamInventoryResult_t * pResultHandle, const char * pchItemToken );

// ISteamTimeline

// A versioned accessor is exported by the library
S_API ISteamTimeline *SteamAPI_SteamTimeline_v001();
// Inline, unversioned accessor to get the current version.  Essentially the same as SteamTimeline(), but using this ensures that you are using a matching library.
inline ISteamTimeline *SteamAPI_SteamTimeline() { return SteamAPI_SteamTimeline_v001(); }
S_API void SteamAPI_ISteamTimeline_SetTimelineStateDescription( ISteamTimeline* self, const char * pchDescription, float flTimeDelta );
S_API void SteamAPI_ISteamTimeline_ClearTimelineStateDescription( ISteamTimeline* self, float flTimeDelta );
S_API void SteamAPI_ISteamTimeline_AddTimelineEvent( ISteamTimeline* self, const char * pchIcon, const char * pchTitle, const char * pchDescription, uint32 unPriority, float flStartOffsetSeconds, float flDurationSeconds, ETimelineEventClipPriority ePossibleClip );
S_API void SteamAPI_ISteamTimeline_SetTimelineGameMode( ISteamTimeline* self, ETimelineGameMode eMode );

// ISteamVideo

// A versioned accessor is exported by the library
S_API ISteamVideo *SteamAPI_SteamVideo_v007();
// Inline, unversioned accessor to get the current version.  Essentially the same as SteamVideo(), but using this ensures that you are using a matching library.
inline ISteamVideo *SteamAPI_SteamVideo() { return SteamAPI_SteamVideo_v007(); }
S_API void SteamAPI_ISteamVideo_GetVideoURL( ISteamVideo* self, AppId_t unVideoAppID );
S_API bool SteamAPI_ISteamVideo_IsBroadcasting( ISteamVideo* self, int * pnNumViewers );
S_API void SteamAPI_ISteamVideo_GetOPFSettings( ISteamVideo* self, AppId_t unVideoAppID );
S_API bool SteamAPI_ISteamVideo_GetOPFStringForApp( ISteamVideo* self, AppId_t unVideoAppID, char * pchBuffer, int32 * pnBufferSize );

// ISteamParentalSettings

// A versioned accessor is exported by the library
S_API ISteamParentalSettings *SteamAPI_SteamParentalSettings_v001();
// Inline, unversioned accessor to get the current version.  Essentially the same as SteamParentalSettings(), but using this ensures that you are using a matching library.
inline ISteamParentalSettings *SteamAPI_SteamParentalSettings() { return SteamAPI_SteamParentalSettings_v001(); }
S_API bool SteamAPI_ISteamParentalSettings_BIsParentalLockEnabled( ISteamParentalSettings* self );
S_API bool SteamAPI_ISteamParentalSettings_BIsParentalLockLocked( ISteamParentalSettings* self );
S_API bool SteamAPI_ISteamParentalSettings_BIsAppBlocked( ISteamParentalSettings* self, AppId_t nAppID );
S_API bool SteamAPI_ISteamParentalSettings_BIsAppInBlockList( ISteamParentalSettings* self, AppId_t nAppID );
S_API bool SteamAPI_ISteamParentalSettings_BIsFeatureBlocked( ISteamParentalSettings* self, EParentalFeature eFeature );
S_API bool SteamAPI_ISteamParentalSettings_BIsFeatureInBlockList( ISteamParentalSettings* self, EParentalFeature eFeature );

// ISteamRemotePlay

// A versioned accessor is exported by the library
S_API ISteamRemotePlay *SteamAPI_SteamRemotePlay_v002();
// Inline, unversioned accessor to get the current version.  Essentially the same as SteamRemotePlay(), but using this ensures that you are using a matching library.
inline ISteamRemotePlay *SteamAPI_SteamRemotePlay() { return SteamAPI_SteamRemotePlay_v002(); }
S_API uint32 SteamAPI_ISteamRemotePlay_GetSessionCount( ISteamRemotePlay* self );
S_API RemotePlaySessionID_t SteamAPI_ISteamRemotePlay_GetSessionID( ISteamRemotePlay* self, int iSessionIndex );
S_API uint64_steamid SteamAPI_ISteamRemotePlay_GetSessionSteamID( ISteamRemotePlay* self, RemotePlaySessionID_t unSessionID );
S_API const char * SteamAPI_ISteamRemotePlay_GetSessionClientName( ISteamRemotePlay* self, RemotePlaySessionID_t unSessionID );
S_API ESteamDeviceFormFactor SteamAPI_ISteamRemotePlay_GetSessionClientFormFactor( ISteamRemotePlay* self, RemotePlaySessionID_t unSessionID );
S_API bool SteamAPI_ISteamRemotePlay_BGetSessionClientResolution( ISteamRemotePlay* self, RemotePlaySessionID_t unSessionID, int * pnResolutionX, int * pnResolutionY );
S_API bool SteamAPI_ISteamRemotePlay_BStartRemotePlayTogether( ISteamRemotePlay* self, bool bShowOverlay );
S_API bool SteamAPI_ISteamRemotePlay_BSendRemotePlayTogetherInvite( ISteamRemotePlay* self, uint64_steamid steamIDFriend );

// ISteamNetworkingMessages

// A versioned accessor is exported by the library
S_API ISteamNetworkingMessages *SteamAPI_SteamNetworkingMessages_SteamAPI_v002();
// Inline, unversioned accessor to get the current version.  Essentially the same as SteamNetworkingMessages_SteamAPI(), but using this ensures that you are using a matching library.
inline ISteamNetworkingMessages *SteamAPI_SteamNetworkingMessages_SteamAPI() { return SteamAPI_SteamNetworkingMessages_SteamAPI_v002(); }

// A versioned accessor is exported by the library
S_API ISteamNetworkingMessages *SteamAPI_SteamGameServerNetworkingMessages_SteamAPI_v002();
// Inline, unversioned accessor to get the current version.  Essentially the same as SteamGameServerNetworkingMessages_SteamAPI(), but using this ensures that you are using a matching library.
inline ISteamNetworkingMessages *SteamAPI_SteamGameServerNetworkingMessages_SteamAPI() { return SteamAPI_SteamGameServerNetworkingMessages_SteamAPI_v002(); }
S_API EResult SteamAPI_ISteamNetworkingMessages_SendMessageToUser( ISteamNetworkingMessages* self, const SteamNetworkingIdentity & identityRemote, const void * pubData, uint32 cubData, int nSendFlags, int nRemoteChannel );
S_API int SteamAPI_ISteamNetworkingMessages_ReceiveMessagesOnChannel( ISteamNetworkingMessages* self, int nLocalChannel, SteamNetworkingMessage_t ** ppOutMessages, int nMaxMessages );
S_API bool SteamAPI_ISteamNetworkingMessages_AcceptSessionWithUser( ISteamNetworkingMessages* self, const SteamNetworkingIdentity & identityRemote );
S_API bool SteamAPI_ISteamNetworkingMessages_CloseSessionWithUser( ISteamNetworkingMessages* self, const SteamNetworkingIdentity & identityRemote );
S_API bool SteamAPI_ISteamNetworkingMessages_CloseChannelWithUser( ISteamNetworkingMessages* self, const SteamNetworkingIdentity & identityRemote, int nLocalChannel );
S_API ESteamNetworkingConnectionState SteamAPI_ISteamNetworkingMessages_GetSessionConnectionInfo( ISteamNetworkingMessages* self, const SteamNetworkingIdentity & identityRemote, SteamNetConnectionInfo_t * pConnectionInfo, SteamNetConnectionRealTimeStatus_t * pQuickStatus );

// ISteamNetworkingSockets

// A versioned accessor is exported by the library
S_API ISteamNetworkingSockets *SteamAPI_SteamNetworkingSockets_SteamAPI_v012();
// Inline, unversioned accessor to get the current version.  Essentially the same as SteamNetworkingSockets_SteamAPI(), but using this ensures that you are using a matching library.
inline ISteamNetworkingSockets *SteamAPI_SteamNetworkingSockets_SteamAPI() { return SteamAPI_SteamNetworkingSockets_SteamAPI_v012(); }

// A versioned accessor is exported by the library
S_API ISteamNetworkingSockets *SteamAPI_SteamGameServerNetworkingSockets_SteamAPI_v012();
// Inline, unversioned accessor to get the current version.  Essentially the same as SteamGameServerNetworkingSockets_SteamAPI(), but using this ensures that you are using a matching library.
inline ISteamNetworkingSockets *SteamAPI_SteamGameServerNetworkingSockets_SteamAPI() { return SteamAPI_SteamGameServerNetworkingSockets_SteamAPI_v012(); }
S_API HSteamListenSocket SteamAPI_ISteamNetworkingSockets_CreateListenSocketIP( ISteamNetworkingSockets* self, const SteamNetworkingIPAddr & localAddress, int nOptions, const SteamNetworkingConfigValue_t * pOptions );
S_API HSteamNetConnection SteamAPI_ISteamNetworkingSockets_ConnectByIPAddress( ISteamNetworkingSockets* self, const SteamNetworkingIPAddr & address, int nOptions, const SteamNetworkingConfigValue_t * pOptions );
S_API HSteamListenSocket SteamAPI_ISteamNetworkingSockets_CreateListenSocketP2P( ISteamNetworkingSockets* self, int nLocalVirtualPort, int nOptions, const SteamNetworkingConfigValue_t * pOptions );
S_API HSteamNetConnection SteamAPI_ISteamNetworkingSockets_ConnectP2P( ISteamNetworkingSockets* self, const SteamNetworkingIdentity & identityRemote, int nRemoteVirtualPort, int nOptions, const SteamNetworkingConfigValue_t * pOptions );
S_API EResult SteamAPI_ISteamNetworkingSockets_AcceptConnection( ISteamNetworkingSockets* self, HSteamNetConnection hConn );
S_API bool SteamAPI_ISteamNetworkingSockets_CloseConnection( ISteamNetworkingSockets* self, HSteamNetConnection hPeer, int nReason, const char * pszDebug, bool bEnableLinger );
S_API bool SteamAPI_ISteamNetworkingSockets_CloseListenSocket( ISteamNetworkingSockets* self, HSteamListenSocket hSocket );
S_API bool SteamAPI_ISteamNetworkingSockets_SetConnectionUserData( ISteamNetworkingSockets* self, HSteamNetConnection hPeer, int64 nUserData );
S_API int64 SteamAPI_ISteamNetworkingSockets_GetConnectionUserData( ISteamNetworkingSockets* self, HSteamNetConnection hPeer );
S_API void SteamAPI_ISteamNetworkingSockets_SetConnectionName( ISteamNetworkingSockets* self, HSteamNetConnection hPeer, const char * pszName );
S_API bool SteamAPI_ISteamNetworkingSockets_GetConnectionName( ISteamNetworkingSockets* self, HSteamNetConnection hPeer, char * pszName, int nMaxLen );
S_API EResult SteamAPI_ISteamNetworkingSockets_SendMessageToConnection( ISteamNetworkingSockets* self, HSteamNetConnection hConn, const void * pData, uint32 cbData, int nSendFlags, int64 * pOutMessageNumber );
S_API void SteamAPI_ISteamNetworkingSockets_SendMessages( ISteamNetworkingSockets* self, int nMessages, SteamNetworkingMessage_t *const * pMessages, int64 * pOutMessageNumberOrResult );
S_API EResult SteamAPI_ISteamNetworkingSockets_FlushMessagesOnConnection( ISteamNetworkingSockets* self, HSteamNetConnection hConn );
S_API int SteamAPI_ISteamNetworkingSockets_ReceiveMessagesOnConnection( ISteamNetworkingSockets* self, HSteamNetConnection hConn, SteamNetworkingMessage_t ** ppOutMessages, int nMaxMessages );
S_API bool SteamAPI_ISteamNetworkingSockets_GetConnectionInfo( ISteamNetworkingSockets* self, HSteamNetConnection hConn, SteamNetConnectionInfo_t * pInfo );
S_API EResult SteamAPI_ISteamNetworkingSockets_GetConnectionRealTimeStatus( ISteamNetworkingSockets* self, HSteamNetConnection hConn, SteamNetConnectionRealTimeStatus_t * pStatus, int nLanes, SteamNetConnectionRealTimeLaneStatus_t * pLanes );
S_API int SteamAPI_ISteamNetworkingSockets_GetDetailedConnectionStatus( ISteamNetworkingSockets* self, HSteamNetConnection hConn, char * pszBuf, int cbBuf );
S_API bool SteamAPI_ISteamNetworkingSockets_GetListenSocketAddress( ISteamNetworkingSockets* self, HSteamListenSocket hSocket, SteamNetworkingIPAddr * address );
S_API bool SteamAPI_ISteamNetworkingSockets_CreateSocketPair( ISteamNetworkingSockets* self, HSteamNetConnection * pOutConnection1, HSteamNetConnection * pOutConnection2, bool bUseNetworkLoopback, const SteamNetworkingIdentity * pIdentity1, const SteamNetworkingIdentity * pIdentity2 );
S_API EResult SteamAPI_ISteamNetworkingSockets_ConfigureConnectionLanes( ISteamNetworkingSockets* self, HSteamNetConnection hConn, int nNumLanes, const int * pLanePriorities, const uint16 * pLaneWeights );
S_API bool SteamAPI_ISteamNetworkingSockets_GetIdentity( ISteamNetworkingSockets* self, SteamNetworkingIdentity * pIdentity );
S_API ESteamNetworkingAvailability SteamAPI_ISteamNetworkingSockets_InitAuthentication( ISteamNetworkingSockets* self );
S_API ESteamNetworkingAvailability SteamAPI_ISteamNetworkingSockets_GetAuthenticationStatus( ISteamNetworkingSockets* self, SteamNetAuthenticationStatus_t * pDetails );
S_API HSteamNetPollGroup SteamAPI_ISteamNetworkingSockets_CreatePollGroup( ISteamNetworkingSockets* self );
S_API bool SteamAPI_ISteamNetworkingSockets_DestroyPollGroup( ISteamNetworkingSockets* self, HSteamNetPollGroup hPollGroup );
S_API bool SteamAPI_ISteamNetworkingSockets_SetConnectionPollGroup( ISteamNetworkingSockets* self, HSteamNetConnection hConn, HSteamNetPollGroup hPollGroup );
S_API int SteamAPI_ISteamNetworkingSockets_ReceiveMessagesOnPollGroup( ISteamNetworkingSockets* self, HSteamNetPollGroup hPollGroup, SteamNetworkingMessage_t ** ppOutMessages, int nMaxMessages );
S_API bool SteamAPI_ISteamNetworkingSockets_ReceivedRelayAuthTicket( ISteamNetworkingSockets* self, const void * pvTicket, int cbTicket, SteamDatagramRelayAuthTicket * pOutParsedTicket );
S_API int SteamAPI_ISteamNetworkingSockets_FindRelayAuthTicketForServer( ISteamNetworkingSockets* self, const SteamNetworkingIdentity & identityGameServer, int nRemoteVirtualPort, SteamDatagramRelayAuthTicket * pOutParsedTicket );
S_API HSteamNetConnection SteamAPI_ISteamNetworkingSockets_ConnectToHostedDedicatedServer( ISteamNetworkingSockets* self, const SteamNetworkingIdentity & identityTarget, int nRemoteVirtualPort, int nOptions, const SteamNetworkingConfigValue_t * pOptions );
S_API uint16 SteamAPI_ISteamNetworkingSockets_GetHostedDedicatedServerPort( ISteamNetworkingSockets* self );
S_API SteamNetworkingPOPID SteamAPI_ISteamNetworkingSockets_GetHostedDedicatedServerPOPID( ISteamNetworkingSockets* self );
S_API EResult SteamAPI_ISteamNetworkingSockets_GetHostedDedicatedServerAddress( ISteamNetworkingSockets* self, SteamDatagramHostedAddress * pRouting );
S_API HSteamListenSocket SteamAPI_ISteamNetworkingSockets_CreateHostedDedicatedServerListenSocket( ISteamNetworkingSockets* self, int nLocalVirtualPort, int nOptions, const SteamNetworkingConfigValue_t * pOptions );
S_API EResult SteamAPI_ISteamNetworkingSockets_GetGameCoordinatorServerLogin( ISteamNetworkingSockets* self, SteamDatagramGameCoordinatorServerLogin * pLoginInfo, int * pcbSignedBlob, void * pBlob );
S_API HSteamNetConnection SteamAPI_ISteamNetworkingSockets_ConnectP2PCustomSignaling( ISteamNetworkingSockets* self, ISteamNetworkingConnectionSignaling * pSignaling, const SteamNetworkingIdentity * pPeerIdentity, int nRemoteVirtualPort, int nOptions, const SteamNetworkingConfigValue_t * pOptions );
S_API bool SteamAPI_ISteamNetworkingSockets_ReceivedP2PCustomSignal( ISteamNetworkingSockets* self, const void * pMsg, int cbMsg, ISteamNetworkingSignalingRecvContext * pContext );
S_API bool SteamAPI_ISteamNetworkingSockets_GetCertificateRequest( ISteamNetworkingSockets* self, int * pcbBlob, void * pBlob, SteamNetworkingErrMsg & errMsg );
S_API bool SteamAPI_ISteamNetworkingSockets_SetCertificate( ISteamNetworkingSockets* self, const void * pCertificate, int cbCertificate, SteamNetworkingErrMsg & errMsg );
S_API void SteamAPI_ISteamNetworkingSockets_ResetIdentity( ISteamNetworkingSockets* self, const SteamNetworkingIdentity * pIdentity );
S_API void SteamAPI_ISteamNetworkingSockets_RunCallbacks( ISteamNetworkingSockets* self );
S_API bool SteamAPI_ISteamNetworkingSockets_BeginAsyncRequestFakeIP( ISteamNetworkingSockets* self, int nNumPorts );
S_API void SteamAPI_ISteamNetworkingSockets_GetFakeIP( ISteamNetworkingSockets* self, int idxFirstPort, SteamNetworkingFakeIPResult_t * pInfo );
S_API HSteamListenSocket SteamAPI_ISteamNetworkingSockets_CreateListenSocketP2PFakeIP( ISteamNetworkingSockets* self, int idxFakePort, int nOptions, const SteamNetworkingConfigValue_t * pOptions );
S_API EResult SteamAPI_ISteamNetworkingSockets_GetRemoteFakeIPForConnection( ISteamNetworkingSockets* self, HSteamNetConnection hConn, SteamNetworkingIPAddr * pOutAddr );
S_API ISteamNetworkingFakeUDPPort * SteamAPI_ISteamNetworkingSockets_CreateFakeUDPPort( ISteamNetworkingSockets* self, int idxFakeServerPort );

// ISteamNetworkingUtils

// A versioned accessor is exported by the library
S_API ISteamNetworkingUtils *SteamAPI_SteamNetworkingUtils_SteamAPI_v004();
// Inline, unversioned accessor to get the current version.  Essentially the same as SteamNetworkingUtils_SteamAPI(), but using this ensures that you are using a matching library.
inline ISteamNetworkingUtils *SteamAPI_SteamNetworkingUtils_SteamAPI() { return SteamAPI_SteamNetworkingUtils_SteamAPI_v004(); }
S_API SteamNetworkingMessage_t * SteamAPI_ISteamNetworkingUtils_AllocateMessage( ISteamNetworkingUtils* self, int cbAllocateBuffer );
S_API void SteamAPI_ISteamNetworkingUtils_InitRelayNetworkAccess( ISteamNetworkingUtils* self );
S_API ESteamNetworkingAvailability SteamAPI_ISteamNetworkingUtils_GetRelayNetworkStatus( ISteamNetworkingUtils* self, SteamRelayNetworkStatus_t * pDetails );
S_API float SteamAPI_ISteamNetworkingUtils_GetLocalPingLocation( ISteamNetworkingUtils* self, SteamNetworkPingLocation_t & result );
S_API int SteamAPI_ISteamNetworkingUtils_EstimatePingTimeBetweenTwoLocations( ISteamNetworkingUtils* self, const SteamNetworkPingLocation_t & location1, const SteamNetworkPingLocation_t & location2 );
S_API int SteamAPI_ISteamNetworkingUtils_EstimatePingTimeFromLocalHost( ISteamNetworkingUtils* self, const SteamNetworkPingLocation_t & remoteLocation );
S_API void SteamAPI_ISteamNetworkingUtils_ConvertPingLocationToString( ISteamNetworkingUtils* self, const SteamNetworkPingLocation_t & location, char * pszBuf, int cchBufSize );
S_API bool SteamAPI_ISteamNetworkingUtils_ParsePingLocationString( ISteamNetworkingUtils* self, const char * pszString, SteamNetworkPingLocation_t & result );
S_API bool SteamAPI_ISteamNetworkingUtils_CheckPingDataUpToDate( ISteamNetworkingUtils* self, float flMaxAgeSeconds );
S_API int SteamAPI_ISteamNetworkingUtils_GetPingToDataCenter( ISteamNetworkingUtils* self, SteamNetworkingPOPID popID, SteamNetworkingPOPID * pViaRelayPoP );
S_API int SteamAPI_ISteamNetworkingUtils_GetDirectPingToPOP( ISteamNetworkingUtils* self, SteamNetworkingPOPID popID );
S_API int SteamAPI_ISteamNetworkingUtils_GetPOPCount( ISteamNetworkingUtils* self );
S_API int SteamAPI_ISteamNetworkingUtils_GetPOPList( ISteamNetworkingUtils* self, SteamNetworkingPOPID * list, int nListSz );
S_API SteamNetworkingMicroseconds SteamAPI_ISteamNetworkingUtils_GetLocalTimestamp( ISteamNetworkingUtils* self );
S_API void SteamAPI_ISteamNetworkingUtils_SetDebugOutputFunction( ISteamNetworkingUtils* self, ESteamNetworkingSocketsDebugOutputType eDetailLevel, FSteamNetworkingSocketsDebugOutput pfnFunc );
S_API bool SteamAPI_ISteamNetworkingUtils_IsFakeIPv4( ISteamNetworkingUtils* self, uint32 nIPv4 );
S_API ESteamNetworkingFakeIPType SteamAPI_ISteamNetworkingUtils_GetIPv4FakeIPType( ISteamNetworkingUtils* self, uint32 nIPv4 );
S_API EResult SteamAPI_ISteamNetworkingUtils_GetRealIdentityForFakeIP( ISteamNetworkingUtils* self, const SteamNetworkingIPAddr & fakeIP, SteamNetworkingIdentity * pOutRealIdentity );
S_API bool SteamAPI_ISteamNetworkingUtils_SetGlobalConfigValueInt32( ISteamNetworkingUtils* self, ESteamNetworkingConfigValue eValue, int32 val );
S_API bool SteamAPI_ISteamNetworkingUtils_SetGlobalConfigValueFloat( ISteamNetworkingUtils* self, ESteamNetworkingConfigValue eValue, float val );
S_API bool SteamAPI_ISteamNetworkingUtils_SetGlobalConfigValueString( ISteamNetworkingUtils* self, ESteamNetworkingConfigValue eValue, const char * val );
S_API bool SteamAPI_ISteamNetworkingUtils_SetGlobalConfigValuePtr( ISteamNetworkingUtils* self, ESteamNetworkingConfigValue eValue, void * val );
S_API bool SteamAPI_ISteamNetworkingUtils_SetConnectionConfigValueInt32( ISteamNetworkingUtils* self, HSteamNetConnection hConn, ESteamNetworkingConfigValue eValue, int32 val );
S_API bool SteamAPI_ISteamNetworkingUtils_SetConnectionConfigValueFloat( ISteamNetworkingUtils* self, HSteamNetConnection hConn, ESteamNetworkingConfigValue eValue, float val );
S_API bool SteamAPI_ISteamNetworkingUtils_SetConnectionConfigValueString( ISteamNetworkingUtils* self, HSteamNetConnection hConn, ESteamNetworkingConfigValue eValue, const char * val );
S_API bool SteamAPI_ISteamNetworkingUtils_SetGlobalCallback_SteamNetConnectionStatusChanged( ISteamNetworkingUtils* self, FnSteamNetConnectionStatusChanged fnCallback );
S_API bool SteamAPI_ISteamNetworkingUtils_SetGlobalCallback_SteamNetAuthenticationStatusChanged( ISteamNetworkingUtils* self, FnSteamNetAuthenticationStatusChanged fnCallback );
S_API bool SteamAPI_ISteamNetworkingUtils_SetGlobalCallback_SteamRelayNetworkStatusChanged( ISteamNetworkingUtils* self, FnSteamRelayNetworkStatusChanged fnCallback );
S_API bool SteamAPI_ISteamNetworkingUtils_SetGlobalCallback_FakeIPResult( ISteamNetworkingUtils* self, FnSteamNetworkingFakeIPResult fnCallback );
S_API bool SteamAPI_ISteamNetworkingUtils_SetGlobalCallback_MessagesSessionRequest( ISteamNetworkingUtils* self, FnSteamNetworkingMessagesSessionRequest fnCallback );
S_API bool SteamAPI_ISteamNetworkingUtils_SetGlobalCallback_MessagesSessionFailed( ISteamNetworkingUtils* self, FnSteamNetworkingMessagesSessionFailed fnCallback );
S_API bool SteamAPI_ISteamNetworkingUtils_SetConfigValue( ISteamNetworkingUtils* self, ESteamNetworkingConfigValue eValue, ESteamNetworkingConfigScope eScopeType, intptr_t scopeObj, ESteamNetworkingConfigDataType eDataType, const void * pArg );
S_API bool SteamAPI_ISteamNetworkingUtils_SetConfigValueStruct( ISteamNetworkingUtils* self, const SteamNetworkingConfigValue_t & opt, ESteamNetworkingConfigScope eScopeType, intptr_t scopeObj );
S_API ESteamNetworkingGetConfigValueResult SteamAPI_ISteamNetworkingUtils_GetConfigValue( ISteamNetworkingUtils* self, ESteamNetworkingConfigValue eValue, ESteamNetworkingConfigScope eScopeType, intptr_t scopeObj, ESteamNetworkingConfigDataType * pOutDataType, void * pResult, size_t * cbResult );
S_API const char * SteamAPI_ISteamNetworkingUtils_GetConfigValueInfo( ISteamNetworkingUtils* self, ESteamNetworkingConfigValue eValue, ESteamNetworkingConfigDataType * pOutDataType, ESteamNetworkingConfigScope * pOutScope );
S_API ESteamNetworkingConfigValue SteamAPI_ISteamNetworkingUtils_IterateGenericEditableConfigValues( ISteamNetworkingUtils* self, ESteamNetworkingConfigValue eCurrent, bool bEnumerateDevVars );
S_API void SteamAPI_ISteamNetworkingUtils_SteamNetworkingIPAddr_ToString( ISteamNetworkingUtils* self, const SteamNetworkingIPAddr & addr, char * buf, uint32 cbBuf, bool bWithPort );
S_API bool SteamAPI_ISteamNetworkingUtils_SteamNetworkingIPAddr_ParseString( ISteamNetworkingUtils* self, SteamNetworkingIPAddr * pAddr, const char * pszStr );
S_API ESteamNetworkingFakeIPType SteamAPI_ISteamNetworkingUtils_SteamNetworkingIPAddr_GetFakeIPType( ISteamNetworkingUtils* self, const SteamNetworkingIPAddr & addr );
S_API void SteamAPI_ISteamNetworkingUtils_SteamNetworkingIdentity_ToString( ISteamNetworkingUtils* self, const SteamNetworkingIdentity & identity, char * buf, uint32 cbBuf );
S_API bool SteamAPI_ISteamNetworkingUtils_SteamNetworkingIdentity_ParseString( ISteamNetworkingUtils* self, SteamNetworkingIdentity * pIdentity, const char * pszStr );

// ISteamGameServer

// A versioned accessor is exported by the library
S_API ISteamGameServer *SteamAPI_SteamGameServer_v015();
// Inline, unversioned accessor to get the current version.  Essentially the same as SteamGameServer(), but using this ensures that you are using a matching library.
inline ISteamGameServer *SteamAPI_SteamGameServer() { return SteamAPI_SteamGameServer_v015(); }
S_API void SteamAPI_ISteamGameServer_SetProduct( ISteamGameServer* self, const char * pszProduct );
S_API void SteamAPI_ISteamGameServer_SetGameDescription( ISteamGameServer* self, const char * pszGameDescription );
S_API void SteamAPI_ISteamGameServer_SetModDir( ISteamGameServer* self, const char * pszModDir );
S_API void SteamAPI_ISteamGameServer_SetDedicatedServer( ISteamGameServer* self, bool bDedicated );
S_API void SteamAPI_ISteamGameServer_LogOn( ISteamGameServer* self, const char * pszToken );
S_API void SteamAPI_ISteamGameServer_LogOnAnonymous( ISteamGameServer* self );
S_API void SteamAPI_ISteamGameServer_LogOff( ISteamGameServer* self );
S_API bool SteamAPI_ISteamGameServer_BLoggedOn( ISteamGameServer* self );
S_API bool SteamAPI_ISteamGameServer_BSecure( ISteamGameServer* self );
S_API uint64_steamid SteamAPI_ISteamGameServer_GetSteamID( ISteamGameServer* self );
S_API bool SteamAPI_ISteamGameServer_WasRestartRequested( ISteamGameServer* self );
S_API void SteamAPI_ISteamGameServer_SetMaxPlayerCount( ISteamGameServer* self, int cPlayersMax );
S_API void SteamAPI_ISteamGameServer_SetBotPlayerCount( ISteamGameServer* self, int cBotplayers );
S_API void SteamAPI_ISteamGameServer_SetServerName( ISteamGameServer* self, const char * pszServerName );
S_API void SteamAPI_ISteamGameServer_SetMapName( ISteamGameServer* self, const char * pszMapName );
S_API void SteamAPI_ISteamGameServer_SetPasswordProtected( ISteamGameServer* self, bool bPasswordProtected );
S_API void SteamAPI_ISteamGameServer_SetSpectatorPort( ISteamGameServer* self, uint16 unSpectatorPort );
S_API void SteamAPI_ISteamGameServer_SetSpectatorServerName( ISteamGameServer* self, const char * pszSpectatorServerName );
S_API void SteamAPI_ISteamGameServer_ClearAllKeyValues( ISteamGameServer* self );
S_API void SteamAPI_ISteamGameServer_SetKeyValue( ISteamGameServer* self, const char * pKey, const char * pValue );
S_API void SteamAPI_ISteamGameServer_SetGameTags( ISteamGameServer* self, const char * pchGameTags );
S_API void SteamAPI_ISteamGameServer_SetGameData( ISteamGameServer* self, const char * pchGameData );
S_API void SteamAPI_ISteamGameServer_SetRegion( ISteamGameServer* self, const char * pszRegion );
S_API void SteamAPI_ISteamGameServer_SetAdvertiseServerActive( ISteamGameServer* self, bool bActive );
S_API HAuthTicket SteamAPI_ISteamGameServer_GetAuthSessionTicket( ISteamGameServer* self, void * pTicket, int cbMaxTicket, uint32 * pcbTicket, const SteamNetworkingIdentity * pSnid );
S_API EBeginAuthSessionResult SteamAPI_ISteamGameServer_BeginAuthSession( ISteamGameServer* self, const void * pAuthTicket, int cbAuthTicket, uint64_steamid steamID );
S_API void SteamAPI_ISteamGameServer_EndAuthSession( ISteamGameServer* self, uint64_steamid steamID );
S_API void SteamAPI_ISteamGameServer_CancelAuthTicket( ISteamGameServer* self, HAuthTicket hAuthTicket );
S_API EUserHasLicenseForAppResult SteamAPI_ISteamGameServer_UserHasLicenseForApp( ISteamGameServer* self, uint64_steamid steamID, AppId_t appID );
S_API bool SteamAPI_ISteamGameServer_RequestUserGroupStatus( ISteamGameServer* self, uint64_steamid steamIDUser, uint64_steamid steamIDGroup );
S_API void SteamAPI_ISteamGameServer_GetGameplayStats( ISteamGameServer* self );
S_API SteamAPICall_t SteamAPI_ISteamGameServer_GetServerReputation( ISteamGameServer* self );
S_API SteamIPAddress_t SteamAPI_ISteamGameServer_GetPublicIP( ISteamGameServer* self );
S_API bool SteamAPI_ISteamGameServer_HandleIncomingPacket( ISteamGameServer* self, const void * pData, int cbData, uint32 srcIP, uint16 srcPort );
S_API int SteamAPI_ISteamGameServer_GetNextOutgoingPacket( ISteamGameServer* self, void * pOut, int cbMaxOut, uint32 * pNetAdr, uint16 * pPort );
S_API SteamAPICall_t SteamAPI_ISteamGameServer_AssociateWithClan( ISteamGameServer* self, uint64_steamid steamIDClan );
S_API SteamAPICall_t SteamAPI_ISteamGameServer_ComputeNewPlayerCompatibility( ISteamGameServer* self, uint64_steamid steamIDNewPlayer );
S_API bool SteamAPI_ISteamGameServer_SendUserConnectAndAuthenticate_DEPRECATED( ISteamGameServer* self, uint32 unIPClient, const void * pvAuthBlob, uint32 cubAuthBlobSize, CSteamID * pSteamIDUser );
S_API uint64_steamid SteamAPI_ISteamGameServer_CreateUnauthenticatedUserConnection( ISteamGameServer* self );
S_API void SteamAPI_ISteamGameServer_SendUserDisconnect_DEPRECATED( ISteamGameServer* self, uint64_steamid steamIDUser );
S_API bool SteamAPI_ISteamGameServer_BUpdateUserData( ISteamGameServer* self, uint64_steamid steamIDUser, const char * pchPlayerName, uint32 uScore );

// ISteamGameServerStats

// A versioned accessor is exported by the library
S_API ISteamGameServerStats *SteamAPI_SteamGameServerStats_v001();
// Inline, unversioned accessor to get the current version.  Essentially the same as SteamGameServerStats(), but using this ensures that you are using a matching library.
inline ISteamGameServerStats *SteamAPI_SteamGameServerStats() { return SteamAPI_SteamGameServerStats_v001(); }
S_API SteamAPICall_t SteamAPI_ISteamGameServerStats_RequestUserStats( ISteamGameServerStats* self, uint64_steamid steamIDUser );
S_API bool SteamAPI_ISteamGameServerStats_GetUserStatInt32( ISteamGameServerStats* self, uint64_steamid steamIDUser, const char * pchName, int32 * pData );
S_API bool SteamAPI_ISteamGameServerStats_GetUserStatFloat( ISteamGameServerStats* self, uint64_steamid steamIDUser, const char * pchName, float * pData );
S_API bool SteamAPI_ISteamGameServerStats_GetUserAchievement( ISteamGameServerStats* self, uint64_steamid steamIDUser, const char * pchName, bool * pbAchieved );
S_API bool SteamAPI_ISteamGameServerStats_SetUserStatInt32( ISteamGameServerStats* self, uint64_steamid steamIDUser, const char * pchName, int32 nData );
S_API bool SteamAPI_ISteamGameServerStats_SetUserStatFloat( ISteamGameServerStats* self, uint64_steamid steamIDUser, const char * pchName, float fData );
S_API bool SteamAPI_ISteamGameServerStats_UpdateUserAvgRateStat( ISteamGameServerStats* self, uint64_steamid steamIDUser, const char * pchName, float flCountThisSession, double dSessionLength );
S_API bool SteamAPI_ISteamGameServerStats_SetUserAchievement( ISteamGameServerStats* self, uint64_steamid steamIDUser, const char * pchName );
S_API bool SteamAPI_ISteamGameServerStats_ClearUserAchievement( ISteamGameServerStats* self, uint64_steamid steamIDUser, const char * pchName );
S_API SteamAPICall_t SteamAPI_ISteamGameServerStats_StoreUserStats( ISteamGameServerStats* self, uint64_steamid steamIDUser );

// ISteamNetworkingFakeUDPPort
S_API void SteamAPI_ISteamNetworkingFakeUDPPort_DestroyFakeUDPPort( ISteamNetworkingFakeUDPPort* self );
S_API EResult SteamAPI_ISteamNetworkingFakeUDPPort_SendMessageToFakeIP( ISteamNetworkingFakeUDPPort* self, const SteamNetworkingIPAddr & remoteAddress, const void * pData, uint32 cbData, int nSendFlags );
S_API int SteamAPI_ISteamNetworkingFakeUDPPort_ReceiveMessages( ISteamNetworkingFakeUDPPort* self, SteamNetworkingMessage_t ** ppOutMessages, int nMaxMessages );
S_API void SteamAPI_ISteamNetworkingFakeUDPPort_ScheduleCleanup( ISteamNetworkingFakeUDPPort* self, const SteamNetworkingIPAddr & remoteAddress );

// SteamIPAddress_t
S_API bool SteamAPI_SteamIPAddress_t_IsSet( SteamIPAddress_t* self );

// MatchMakingKeyValuePair_t
S_API void SteamAPI_MatchMakingKeyValuePair_t_Construct( MatchMakingKeyValuePair_t* self );

// servernetadr_t
S_API void SteamAPI_servernetadr_t_Construct( servernetadr_t* self );
S_API void SteamAPI_servernetadr_t_Init( servernetadr_t* self, unsigned int ip, uint16 usQueryPort, uint16 usConnectionPort );
S_API uint16 SteamAPI_servernetadr_t_GetQueryPort( servernetadr_t* self );
S_API void SteamAPI_servernetadr_t_SetQueryPort( servernetadr_t* self, uint16 usPort );
S_API uint16 SteamAPI_servernetadr_t_GetConnectionPort( servernetadr_t* self );
S_API void SteamAPI_servernetadr_t_SetConnectionPort( servernetadr_t* self, uint16 usPort );
S_API uint32 SteamAPI_servernetadr_t_GetIP( servernetadr_t* self );
S_API void SteamAPI_servernetadr_t_SetIP( servernetadr_t* self, uint32 unIP );
S_API const char * SteamAPI_servernetadr_t_GetConnectionAddressString( servernetadr_t* self );
S_API const char * SteamAPI_servernetadr_t_GetQueryAddressString( servernetadr_t* self );
S_API bool SteamAPI_servernetadr_t_IsLessThan( servernetadr_t* self, const servernetadr_t & netadr );
S_API void SteamAPI_servernetadr_t_Assign( servernetadr_t* self, const servernetadr_t & that );

// gameserveritem_t
S_API void SteamAPI_gameserveritem_t_Construct( gameserveritem_t* self );
S_API const char * SteamAPI_gameserveritem_t_GetName( gameserveritem_t* self );
S_API void SteamAPI_gameserveritem_t_SetName( gameserveritem_t* self, const char * pName );

// SteamNetworkingIPAddr
S_API void SteamAPI_SteamNetworkingIPAddr_Clear( SteamNetworkingIPAddr* self );
S_API bool SteamAPI_SteamNetworkingIPAddr_IsIPv6AllZeros( SteamNetworkingIPAddr* self );
S_API void SteamAPI_SteamNetworkingIPAddr_SetIPv6( SteamNetworkingIPAddr* self, const uint8 * ipv6, uint16 nPort );
S_API void SteamAPI_SteamNetworkingIPAddr_SetIPv4( SteamNetworkingIPAddr* self, uint32 nIP, uint16 nPort );
S_API bool SteamAPI_SteamNetworkingIPAddr_IsIPv4( SteamNetworkingIPAddr* self );
S_API uint32 SteamAPI_SteamNetworkingIPAddr_GetIPv4( SteamNetworkingIPAddr* self );
S_API void SteamAPI_SteamNetworkingIPAddr_SetIPv6LocalHost( SteamNetworkingIPAddr* self, uint16 nPort );
S_API bool SteamAPI_SteamNetworkingIPAddr_IsLocalHost( SteamNetworkingIPAddr* self );
S_API void SteamAPI_SteamNetworkingIPAddr_ToString( SteamNetworkingIPAddr* self, char * buf, uint32 cbBuf, bool bWithPort );
S_API bool SteamAPI_SteamNetworkingIPAddr_ParseString( SteamNetworkingIPAddr* self, const char * pszStr );
S_API bool SteamAPI_SteamNetworkingIPAddr_IsEqualTo( SteamNetworkingIPAddr* self, const SteamNetworkingIPAddr & x );
S_API ESteamNetworkingFakeIPType SteamAPI_SteamNetworkingIPAddr_GetFakeIPType( SteamNetworkingIPAddr* self );
S_API bool SteamAPI_SteamNetworkingIPAddr_IsFakeIP( SteamNetworkingIPAddr* self );

// SteamNetworkingIdentity
S_API void SteamAPI_SteamNetworkingIdentity_Clear( SteamNetworkingIdentity* self );
S_API bool SteamAPI_SteamNetworkingIdentity_IsInvalid( SteamNetworkingIdentity* self );
S_API void SteamAPI_SteamNetworkingIdentity_SetSteamID( SteamNetworkingIdentity* self, uint64_steamid steamID );
S_API uint64_steamid SteamAPI_SteamNetworkingIdentity_GetSteamID( SteamNetworkingIdentity* self );
S_API void SteamAPI_SteamNetworkingIdentity_SetSteamID64( SteamNetworkingIdentity* self, uint64 steamID );
S_API uint64 SteamAPI_SteamNetworkingIdentity_GetSteamID64( SteamNetworkingIdentity* self );
S_API bool SteamAPI_SteamNetworkingIdentity_SetXboxPairwiseID( SteamNetworkingIdentity* self, const char * pszString );
S_API const char * SteamAPI_SteamNetworkingIdentity_GetXboxPairwiseID( SteamNetworkingIdentity* self );
S_API void SteamAPI_SteamNetworkingIdentity_SetPSNID( SteamNetworkingIdentity* self, uint64 id );
S_API uint64 SteamAPI_SteamNetworkingIdentity_GetPSNID( SteamNetworkingIdentity* self );
S_API void SteamAPI_SteamNetworkingIdentity_SetStadiaID( SteamNetworkingIdentity* self, uint64 id );
S_API uint64 SteamAPI_SteamNetworkingIdentity_GetStadiaID( SteamNetworkingIdentity* self );
S_API void SteamAPI_SteamNetworkingIdentity_SetIPAddr( SteamNetworkingIdentity* self, const SteamNetworkingIPAddr & addr );
S_API const SteamNetworkingIPAddr * SteamAPI_SteamNetworkingIdentity_GetIPAddr( SteamNetworkingIdentity* self );
S_API void SteamAPI_SteamNetworkingIdentity_SetIPv4Addr( SteamNetworkingIdentity* self, uint32 nIPv4, uint16 nPort );
S_API uint32 SteamAPI_SteamNetworkingIdentity_GetIPv4( SteamNetworkingIdentity* self );
S_API ESteamNetworkingFakeIPType SteamAPI_SteamNetworkingIdentity_GetFakeIPType( SteamNetworkingIdentity* self );
S_API bool SteamAPI_SteamNetworkingIdentity_IsFakeIP( SteamNetworkingIdentity* self );
S_API void SteamAPI_SteamNetworkingIdentity_SetLocalHost( SteamNetworkingIdentity* self );
S_API bool SteamAPI_SteamNetworkingIdentity_IsLocalHost( SteamNetworkingIdentity* self );
S_API bool SteamAPI_SteamNetworkingIdentity_SetGenericString( SteamNetworkingIdentity* self, const char * pszString );
S_API const char * SteamAPI_SteamNetworkingIdentity_GetGenericString( SteamNetworkingIdentity* self );
S_API bool SteamAPI_SteamNetworkingIdentity_SetGenericBytes( SteamNetworkingIdentity* self, const void * data, uint32 cbLen );
S_API const uint8 * SteamAPI_SteamNetworkingIdentity_GetGenericBytes( SteamNetworkingIdentity* self, int & cbLen );
S_API bool SteamAPI_SteamNetworkingIdentity_IsEqualTo( SteamNetworkingIdentity* self, const SteamNetworkingIdentity & x );
S_API void SteamAPI_SteamNetworkingIdentity_ToString( SteamNetworkingIdentity* self, char * buf, uint32 cbBuf );
S_API bool SteamAPI_SteamNetworkingIdentity_ParseString( SteamNetworkingIdentity* self, const char * pszStr );

// SteamNetworkingMessage_t
S_API void SteamAPI_SteamNetworkingMessage_t_Release( SteamNetworkingMessage_t* self );

// SteamNetworkingConfigValue_t
S_API void SteamAPI_SteamNetworkingConfigValue_t_SetInt32( SteamNetworkingConfigValue_t* self, ESteamNetworkingConfigValue eVal, int32_t data );
S_API void SteamAPI_SteamNetworkingConfigValue_t_SetInt64( SteamNetworkingConfigValue_t* self, ESteamNetworkingConfigValue eVal, int64_t data );
S_API void SteamAPI_SteamNetworkingConfigValue_t_SetFloat( SteamNetworkingConfigValue_t* self, ESteamNetworkingConfigValue eVal, float data );
S_API void SteamAPI_SteamNetworkingConfigValue_t_SetPtr( SteamNetworkingConfigValue_t* self, ESteamNetworkingConfigValue eVal, void * data );
S_API void SteamAPI_SteamNetworkingConfigValue_t_SetString( SteamNetworkingConfigValue_t* self, ESteamNetworkingConfigValue eVal, const char * data );

// SteamDatagramHostedAddress
S_API void SteamAPI_SteamDatagramHostedAddress_Clear( SteamDatagramHostedAddress* self );
S_API SteamNetworkingPOPID SteamAPI_SteamDatagramHostedAddress_GetPopID( SteamDatagramHostedAddress* self );
S_API void SteamAPI_SteamDatagramHostedAddress_SetDevAddress( SteamDatagramHostedAddress* self, uint32 nIP, uint16 nPort, SteamNetworkingPOPID popid );

#if defined(__cplusplus)
}
#endif
