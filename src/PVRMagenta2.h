/*
 *  Copyright (C) 2011-2021 Team Kodi (https://kodi.tv)
 *  Copyright (C) 2011 Pulse-Eight (http://www.pulse-eight.com/)
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */
#include <string>
#include <vector>

#include <kodi/addon-instance/PVR.h>
#include <kodi/tools/Timer.h>
#include "Settings.h"
#include "http/HttpClient.h"
#include "sam3/Sam3Client.h"
#include "taa/TaaClient.h"
#include "rapidjson/document.h"
#include <tinyxml2.h>

static const std::string BOOTSTRAP_URL = "https://prod.dcm.telekom-dienste.de/v1/settings/{configGroupId}/bootstrap?";
static const std::string WINDOWS_USER_AGENT = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36";

static const int MAX_CHANNEL_ENTRIES = 100;
static const uint64_t TIMEBUFFER2 = 4 * 60 * 60; //4h time buffer
static const long KBM2 = 150000; // 150 MB
static const int CUTOFF = 1000;

static const std::vector<std::string> Magenta2StationThumbnailTypes
                  = { "stationBackground", "stationBarker", "stationLogo", "stationLogoColored" };

struct Magenta2SubGenre
{
  std::string secondaryGenre;
  int secondaryGenreType;
};

struct Magenta2Genre
{
  std::string primaryGenre;
  int genreType;
  std::vector<Magenta2SubGenre> secondaryGenres;
  int genreSubType;
};

struct Magenta2Lock
{
  std::string instance;
  std::string token;
  std::string id;
  std::string lock;
  std::string sequenceToken;
  std::string serviceUrl;
  int updateInterval;
};

struct Magenta2KV
{
  std::string key;
  std::string value;
};

struct Magenta2Ngiss
{
  std::string basicUrl;
  std::string callParameter;
};

struct Magenta2Picture
{
  std::string title;
  std::string url;
  int width;
  int height;
};

struct Magenta2Channel
{
  bool bRadio;
  std::string id;
  std::string stationsId;
  int iUniqueId;
  int iChannelNumber; //position
  std::string title;
  std::string strChannelName;
  std::string strIconPath;
  std::string mediaPath;
  std::vector<std::string> distributionRights;
  std::string dtquality;
  bool isHidden;
  bool isHd;
  bool isFavorite;
  std::vector<Magenta2Picture> thumbnails;
//  bool isEntitled;
};

struct Magenta2Category
{
  std::string id;
  std::string description;
  std::string parentId;
  int order;
  std::string scheme;
  int level;
  std::vector<int> channelUids;
};
/*
struct Magenta2Recording
{

};
*/
class CPVRMagenta2
{
public:
  CPVRMagenta2(CSettings* settings, HttpClient* httpclient, kodi::addon::CInstancePVRClient* instancePVRClient);
  ~CPVRMagenta2();

  typedef void (CPVRMagenta2::*handleentry_t)(const rapidjson::Value& entry);

  PVR_ERROR GetCapabilities(kodi::addon::PVRCapabilities& capabilities);
  //Channels
  PVR_ERROR GetChannelsAmount(int& amount);
  PVR_ERROR GetChannels(bool bRadio, kodi::addon::PVRChannelsResultSet& results);
  PVR_ERROR GetChannelStreamProperties(
      const kodi::addon::PVRChannel& channel,
      std::vector<kodi::addon::PVRStreamProperty>& properties);
  //EPG
  PVR_ERROR GetEPGForChannel(int channelUid,
                             time_t start,
                             time_t end,
                             kodi::addon::PVREPGTagsResultSet& results);
  PVR_ERROR IsEPGTagPlayable(const kodi::addon::PVREPGTag& tag, bool& bIsPlayable);
  PVR_ERROR GetEPGTagStreamProperties(
      const kodi::addon::PVREPGTag& tag,
      std::vector<kodi::addon::PVRStreamProperty>& properties);
  //Groups
  PVR_ERROR GetChannelGroupsAmount(int& amount);
  PVR_ERROR GetChannelGroups(bool bRadio, kodi::addon::PVRChannelGroupsResultSet& results);
  PVR_ERROR GetChannelGroupMembers(const kodi::addon::PVRChannelGroup& group,
                                   kodi::addon::PVRChannelGroupMembersResultSet& results);
  //Timers
  PVR_ERROR GetTimerTypes(std::vector<kodi::addon::PVRTimerType>& types);
  PVR_ERROR GetTimersAmount(int& amount);
  PVR_ERROR GetTimers(kodi::addon::PVRTimersResultSet& results);
  PVR_ERROR AddTimer(const kodi::addon::PVRTimer& timer);
  PVR_ERROR UpdateTimer(const kodi::addon::PVRTimer& timer);
  PVR_ERROR DeleteTimer(const kodi::addon::PVRTimer& timer, bool forceDelete);
  //Recordings
  PVR_ERROR GetRecordingsAmount(bool deleted, int& amount);
  PVR_ERROR GetRecordings(bool deleted, kodi::addon::PVRRecordingsResultSet& results);
  PVR_ERROR DeleteRecording(const kodi::addon::PVRRecording& recording);
  PVR_ERROR GetRecordingStreamProperties(
      const kodi::addon::PVRRecording& recording,
      std::vector<kodi::addon::PVRStreamProperty>& properties);
  PVR_ERROR GetDriveSpace(uint64_t& total, uint64_t& used);

private:
  PVR_ERROR SetStreamProperties(std::vector<kodi::addon::PVRStreamProperty>& properties,
                                const std::string& url,
                                bool realtime, bool playTimeshiftBuffer, bool epgplayback);

  std::vector<Magenta2Channel> m_channels;
  std::vector<std::string> m_distributionRights;
  std::vector<Magenta2KV> m_parameters;
  std::vector<Magenta2Genre> m_genres;
  std::vector<Magenta2Category> m_categories;
  std::vector<kodi::addon::PVRTimer> m_seriesTimers;
  
  std::unique_ptr<kodi::tools::CTimer> m_refreshTimer;

  HttpClient* m_httpClient;
  CSettings* m_settings;
  Sam3Client* m_sam3Client;
  AuthClient* m_authClient;
  kodi::addon::CInstancePVRClient* m_instancePVRClient;

  bool XMLGetString(const tinyxml2::XMLNode* pRootNode,
                              const std::string& strTag,
                              std::string& strStringValue);

  bool GetMyGenres();
  bool GetUserList(const std::string& context);
  bool SendDeleteRequest(const std::string& url);
  bool GetPostJson(const std::string& url, const std::string& body, rapidjson::Document& doc);
  bool GetSmil(const std::string& url, tinyxml2::XMLDocument& smilDoc);
  bool GetStreamParameters(const std::string& url, std::string& src, std::string& releasePid);
  bool GetParameter(const std::string& key, std::string& value);
  bool Bootstrap();
  bool DeviceManifest();
  bool Manifest();
  bool GetDistributionRights();
  bool GetCategories();
  void SetChannelNumber(const std::string& id, const int& number);
  void SetFavorite(const std::string& id, const int& number);
  std::string GetNgissUrl(const std::string& url, const int& width, const int& height);
  void AddChannelEntry(const rapidjson::Value& entry);
  void AddGroupChannel(const std::string& id, const int& channelUid);
  void AddEntitlementEntry(const rapidjson::Value& entry);
  bool GetFeed(/*const int& feed,*/ const int& maxEntries, /*const std::string& params,*/ std::string& baseUrl/*, kodi::addon::PVREPGTagsResultSet& results*/,
                handleentry_t HandleEntry);
  bool GetGenre(int& primaryType, int& secondaryType, const std::string& primaryGenre, const std::string& secondaryGenre);
  void AddEPGEntry(const int& channelNumber, const rapidjson::Value& entry, kodi::addon::PVREPGTagsResultSet& results);
  bool GetEPGFeed(const int& channelNumber, const std::string& baseUrl, kodi::addon::PVREPGTagsResultSet& results);
  bool GetChannelNamebyId(const std::string& id, std::string& name);
  bool AddDistributionRight(const unsigned int number, const std::string& right);
  bool HideDuplicateChannels();
//  bool SingleSignOn();
  bool ReleaseLock();
  std::string GetSeriesGuidFromSeriesIdUrl(std::string url);
  int GetSeriesTimerIdBySeriesId(std::string seriesId);
  PVR_ERROR AddOncePVRTimer(kodi::addon::PVRTimersResultSet& results);
  PVR_ERROR AddSeriesPVRTimer(kodi::addon::PVRTimersResultSet& results);
  PVR_ERROR GetListingAndSeriesGuidFromAllChannelSchedulesFeed(const kodi::addon::PVRTimer timer, std::string& listingGuid, std::string& seriesGuid, std::string channelGuid);
  int CountTimersRecordings(const bool& isRecording);
  void FillPVRRecording(const rapidjson::Value& recordingItem, kodi::addon::PVRRecording& kodiRecording);
  void SetGenreTypes(const rapidjson::Value& item, std::string& primary, std::string& secondary);
  void OnRefreshEveryMinute();

  std::string m_deviceId;
  std::string m_sessionId;
  std::string m_clientModel;
  std::string m_deviceModel;
  std::string m_manifestBaseUrl;
  std::string m_accountBaseUrl;
  std::string m_deviceTokensUrl;
//  std::string m_sam3ClientId;
  std::string m_entitledChannelsFeed;
  std::string m_allChannelSchedulesFeed;
  std::string m_allChannelStationsFeed;
  std::string m_allListingsFeedUrl;
  std::string m_allProgramsFeedUrl;
  std::string m_liveTvCategoryFeed;
  std::string m_accountPid;
  std::string m_pvrBaseUrl;
  std::string m_basicUrlGetApplicableDistributionRights;
  std::string m_basicUrlSelectorService;
  std::string m_widevineLicenseAcquisitionUrl;
  std::string m_userId;
  std::string m_displayName;
  std::string m_accountId;
  std::string m_locationIdUri;
  std::string m_mpxAccountUri;
  std::string m_userProfileUrl;
  int m_platform;
  Magenta2Lock m_currentLock;
  Magenta2Ngiss m_ngiss;
};
