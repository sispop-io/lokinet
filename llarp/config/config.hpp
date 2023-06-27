#pragma once

#include <chrono>
#include <llarp/bootstrap.hpp>
#include <llarp/crypto/types.hpp>
#include <llarp/router_contact.hpp>
#include <llarp/util/fs.hpp>
#include <llarp/util/str.hpp>
#include <llarp/util/logging.hpp>
#include "ini.hpp"
#include "definition.hpp"
#include <llarp/constants/files.hpp>
#include <llarp/net/ip_address.hpp>
#include <llarp/net/net_int.hpp>
#include <llarp/net/ip_range_map.hpp>
#include <llarp/service/address.hpp>
#include <llarp/service/auth.hpp>
#include <llarp/dns/srv_data.hpp>

#include <llarp/router_contact.hpp>

#include <cstdlib>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <unordered_set>

#include <sispopmq/address.h>

namespace llarp
{
  using SectionValues_t = llarp::ConfigParser::SectionValues_t;
  using Config_impl_t = llarp::ConfigParser::Config_impl_t;

  // TODO: don't use these maps. they're sloppy and difficult to follow
  /// Small struct to gather all parameters needed for config generation to reduce the number of
  /// parameters that need to be passed around.
  struct ConfigGenParameters
  {
    ConfigGenParameters() = default;
    virtual ~ConfigGenParameters() = default;

    ConfigGenParameters(const ConfigGenParameters&) = delete;
    ConfigGenParameters(ConfigGenParameters&&) = delete;

    bool isRelay = false;
    fs::path defaultDataDir;

    /// get network platform (virtual for unit test mocks)
    virtual const llarp::net::Platform*
    Net_ptr() const = 0;
  };

  struct RouterConfig
  {
    size_t m_minConnectedRouters = 0;
    size_t m_maxConnectedRouters = 0;

    std::string m_netId;
    std::string m_nickname;

    fs::path m_dataDir;

    bool m_blockBogons = false;

    int m_workerThreads = -1;
    int m_numNetThreads = -1;

    size_t m_JobQueueSize = 0;

    std::string m_routerContactFile;
    std::string m_encryptionKeyFile;
    std::string m_identityKeyFile;
    std::string m_transportKeyFile;

    bool m_isRelay = false;
    /// deprecated
    std::optional<net::ipaddr_t> PublicIP;
    /// deprecated
    std::optional<net::port_t> PublicPort;

    void
    defineConfigOptions(ConfigDefinition& conf, const ConfigGenParameters& params);
  };

  /// config for path hop selection
  struct PeerSelectionConfig
  {
    /// in our hops what netmask will we use for unique ips for hops
    /// i.e. 32 for every hop unique ip, 24 unique /24 per hop, etc
    ///
    int m_UniqueHopsNetmaskSize;

    /// set of countrys to exclude from path building (2 char country code)
    std::unordered_set<std::string> m_ExcludeCountries;

    void
    defineConfigOptions(ConfigDefinition& conf, const ConfigGenParameters& params);

    /// return true if this set of router contacts is acceptable against this config
    bool
    Acceptable(const std::set<RouterContact>& hops) const;
  };

  struct NetworkConfig
  {
    std::optional<bool> m_enableProfiling;
    bool m_saveProfiles;
    std::set<RouterID> m_strictConnect;
    std::string m_ifname;
    IPRange m_ifaddr;

    std::optional<fs::path> m_keyfile;
    std::string m_endpointType;
    bool m_reachable = false;
    std::optional<int> m_Hops;
    std::optional<int> m_Paths;
    bool m_AllowExit = false;
    std::set<RouterID> m_snodeBlacklist;
    net::IPRangeMap<service::Address> m_ExitMap;
    net::IPRangeMap<std::string> m_LNSExitMap;

    std::unordered_map<service::Address, service::AuthInfo> m_ExitAuths;
    std::unordered_map<std::string, service::AuthInfo> m_LNSExitAuths;

    std::unordered_map<huint128_t, service::Address> m_mapAddrs;

    service::AuthType m_AuthType = service::AuthType::eAuthTypeNone;
    service::AuthFileType m_AuthFileType = service::AuthFileType::eAuthFileHashes;
    std::optional<std::string> m_AuthUrl;
    std::optional<std::string> m_AuthMethod;
    std::unordered_set<service::Address> m_AuthWhitelist;
    std::unordered_set<std::string> m_AuthStaticTokens;
    std::set<fs::path> m_AuthFiles;

    std::vector<llarp::dns::SRVData> m_SRVRecords;

    std::optional<huint128_t> m_baseV6Address;

    std::set<IPRange> m_OwnedRanges;
    std::optional<net::TrafficPolicy> m_TrafficPolicy;

    std::optional<llarp_time_t> m_PathAlignmentTimeout;

    std::optional<fs::path> m_AddrMapPersistFile;

    bool m_EnableRoutePoker;
    bool m_BlackholeRoutes;

    void
    defineConfigOptions(ConfigDefinition& conf, const ConfigGenParameters& params);
  };

  struct DnsConfig
  {
    bool m_raw_dns;
    std::vector<SockAddr> m_bind;
    std::vector<SockAddr> m_upstreamDNS;
    std::vector<fs::path> m_hostfiles;
    std::optional<SockAddr> m_QueryBind;

    std::unordered_multimap<std::string, std::string> m_ExtraOpts;

    void
    defineConfigOptions(ConfigDefinition& conf, const ConfigGenParameters& params);
  };

  struct LinksConfig
  {
    std::optional<net::ipaddr_t> PublicAddress;
    std::optional<net::port_t> PublicPort;
    std::vector<SockAddr> OutboundLinks;
    std::vector<SockAddr> InboundListenAddrs;

    void
    defineConfigOptions(ConfigDefinition& conf, const ConfigGenParameters& params);
  };

  struct ConnectConfig
  {
    std::vector<fs::path> routers;

    void
    defineConfigOptions(ConfigDefinition& conf, const ConfigGenParameters& params);
  };

  struct ApiConfig
  {
    bool m_enableRPCServer = false;
    std::string m_rpcBindAddr;

    void
    defineConfigOptions(ConfigDefinition& conf, const ConfigGenParameters& params);
  };

  struct LokidConfig
  {
    bool whitelistRouters = false;
    fs::path ident_keyfile;
    sispopmq::address lokidRPCAddr;

    void
    defineConfigOptions(ConfigDefinition& conf, const ConfigGenParameters& params);
  };

  struct BootstrapConfig
  {
    std::vector<fs::path> files;
    BootstrapList routers;
    bool seednode;
    void
    defineConfigOptions(ConfigDefinition& conf, const ConfigGenParameters& params);
  };

  struct LoggingConfig
  {
    log::Type m_logType = log::Type::Print;
    log::Level m_logLevel = log::Level::off;
    std::string m_logFile;

    void
    defineConfigOptions(ConfigDefinition& conf, const ConfigGenParameters& params);
  };

  struct Config
  {
    explicit Config(std::optional<fs::path> datadir = std::nullopt);

    virtual ~Config() = default;

    /// create generation params (virtual for unit test mock)
    virtual std::unique_ptr<ConfigGenParameters>
    MakeGenParams() const;

    RouterConfig router;
    NetworkConfig network;
    PeerSelectionConfig paths;
    ConnectConfig connect;
    DnsConfig dns;
    LinksConfig links;
    ApiConfig api;
    LokidConfig lokid;
    BootstrapConfig bootstrap;
    LoggingConfig logging;

    // Initialize config definition
    void
    initializeConfig(ConfigDefinition& conf, const ConfigGenParameters& params);

    /// Insert config entries for backwards-compatibility (e.g. so that the config system will
    /// tolerate old values that are no longer accepted)
    ///
    /// @param conf is the config to modify
    void
    addBackwardsCompatibleConfigOptions(ConfigDefinition& conf);

    // Load a config from the given file if the config file is not provided LoadDefault is called
    bool
    Load(std::optional<fs::path> fname = std::nullopt, bool isRelay = false);

    // Load a config from a string of ini, same effects as Config::Load
    bool
    LoadString(std::string_view ini, bool isRelay = false);

    std::string
    generateBaseClientConfig();

    std::string
    generateBaseRouterConfig();

    void
    Save();

    void
    Override(std::string section, std::string key, std::string value);

    void
    AddDefault(std::string section, std::string key, std::string value);

    /// create a config with the default parameters for an embedded lokinet
    static std::shared_ptr<Config>
    EmbeddedConfig();

   private:
    /// Load (initialize) a default config.
    ///
    /// This delegates to the ConfigDefinition to generate a default config,
    /// as though an empty config were specified.
    ///
    /// If using Config without the intention of loading from file (or string), this is necessary
    /// in order to obtain sane defaults.
    ///
    /// @param isRelay determines whether the config will reflect that of a relay or client
    /// @param dataDir is a path representing a directory to be used as the data dir
    /// @return true on success, false otherwise
    bool
    LoadDefault(bool isRelay);

    bool
    LoadConfigData(
        std::string_view ini, std::optional<fs::path> fname = std::nullopt, bool isRelay = false);

    void
    LoadOverrides(ConfigDefinition& conf) const;

    std::vector<std::array<std::string, 3>> m_Additional;
    ConfigParser m_Parser;
    const fs::path m_DataDir;
  };

  void
  ensureConfig(fs::path dataDir, fs::path confFile, bool overwrite, bool asRouter);

}  // namespace llarp
