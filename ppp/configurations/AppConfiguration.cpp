#include <ppp/configurations/AppConfiguration.h>
#include <ppp/cryptography/Ciphertext.h>
#include <ppp/threading/Thread.h>
#include <ppp/threading/Executors.h>
#include <ppp/io/File.h>
#include <ppp/ssl/SSL.h>
#include <ppp/net/Ipep.h>
#include <ppp/net/IPEndPoint.h>
#include <ppp/auxiliary/JsonAuxiliary.h>
#include <ppp/auxiliary/StringAuxiliary.h>

using ppp::auxiliary::StringAuxiliary;
using ppp::auxiliary::JsonAuxiliary;
using ppp::cryptography::Ciphertext;
using ppp::io::File;
using ppp::io::FileAccess;
using ppp::net::Ipep;
using ppp::net::AddressFamily;
using ppp::net::IPEndPoint;
using ppp::threading::Thread;
using ppp::threading::Executors;

static constexpr int         PPP_DEFAULT_DNS_TIMEOUT = 4;
static constexpr const char* PPP_DEFAULT_KEY_PROTOCOL = "aes-128-cfb";
static constexpr const char* PPP_DEFAULT_KEY_TRANSPORT = "aes-256-cfb";
static constexpr int         PPP_DEFAULT_HTTP_PROXY_PORT = 8080;

namespace ppp {
    namespace configurations {
        AppConfiguration::AppConfiguration() noexcept {
            Clear();
        }

        void AppConfiguration::Clear() noexcept {
            AppConfiguration& config = *this;
            config.concurrent = Thread::GetProcessorCount();
            config.cdn[0] = IPEndPoint::MinPort;
            config.cdn[1] = IPEndPoint::MinPort;

            config.ip.public_ = "";
            config.ip.interface_ = "";
            config.udp.dns.timeout = PPP_DEFAULT_DNS_TIMEOUT;
            config.udp.dns.redirect = "";
            config.udp.inactive.timeout = PPP_UDP_INACTIVE_TIMEOUT;

            config.tcp.turbo = false;
            config.tcp.backlog = PPP_LISTEN_BACKLOG;
            config.tcp.fast_open = false;
            config.tcp.listen.port = IPEndPoint::MinPort;
            config.tcp.connect.timeout = PPP_TCP_CONNECT_TIMEOUT;
            config.tcp.inactive.timeout = PPP_TCP_INACTIVE_TIMEOUT;

            config.websocket.listen.ws = IPEndPoint::MinPort;
            config.websocket.listen.wss = IPEndPoint::MinPort;
            config.websocket.ssl.verify_peer = true;
            config.websocket.ssl.certificate_file = "";
            config.websocket.ssl.certificate_key_file = "";
            config.websocket.ssl.certificate_chain_file = "";
            config.websocket.ssl.certificate_key_password = "";
            config.websocket.ssl.ciphersuites = GetDefaultCipherSuites();
            config.websocket.host = "";
            config.websocket.path = "";
            config.websocket.http.error = "";
            config.websocket.http.request.clear();
            config.websocket.http.response.clear();

            config.key.kf = 154543927;
            config.key.kh = 128;
            config.key.kl = 10;
            config.key.kx = 12;
            config.key.protocol = PPP_DEFAULT_KEY_PROTOCOL;
            config.key.protocol_key = BOOST_BEAST_VERSION_STRING;
            config.key.transport = PPP_DEFAULT_KEY_TRANSPORT;
            config.key.transport_key = BOOST_BEAST_VERSION_STRING;
            config.key.masked = true;
            config.key.plaintext = true;
            config.key.delta_encode = true;
            config.key.shuffle_data = true;

            config.server.log = "";
            config.server.node = 0;
            config.server.subnet = true;
            config.server.mapping = true;
            config.server.backend = "";
            config.server.backend_key = "";

            config.client.mappings.clear();
            config.client.guid = StringAuxiliary::Int128ToGuidString(MAKE_OWORD(UINT64_MAX, UINT64_MAX));
            config.client.server = "";
            config.client.bandwidth = 0;
            config.client.reconnections.timeout = PPP_TCP_CONNECT_TIMEOUT;
            config.client.http_proxy.bind = "";
            config.client.http_proxy.port = PPP_DEFAULT_HTTP_PROXY_PORT;
#if defined(_WIN32)
            config.client.paper_airplane.tcp = true;
#endif
        }

        template <class _Uty>
        static void LRTrim(_Uty* s, int length) noexcept {
            for (int i = 0; i < length; i++) {
                *s[i] = LTrim(RTrim(*s[i]));
            }
        }

        static void LRTrim(AppConfiguration& config, int level) noexcept {
            if (level) {
                ppp::string* strings[] = {
                    &config.ip.public_,
                    &config.ip.interface_,
                    &config.udp.dns.redirect,
                    &config.vmem.path,
                    &config.server.backend,
                    &config.client.guid,
                    &config.client.server,
                    &config.websocket.host,
                    &config.websocket.path,
                    &config.key.protocol,
                    &config.key.protocol_key,
                    &config.key.transport,
                    &config.key.transport_key,
                };
                LRTrim(strings, arraysizeof(strings));
            }
            else {
                std::string* strings[] = {
                    &config.websocket.ssl.certificate_file,
                    &config.websocket.ssl.certificate_key_file,
                    &config.websocket.ssl.certificate_chain_file,
                    &config.websocket.ssl.certificate_key_password,
                    &config.websocket.ssl.ciphersuites,
                };
                LRTrim(strings, arraysizeof(strings));
            }
        }

        static bool LoadAllMappings(AppConfiguration& config, Json::Value& json) noexcept {
            using MappingConfiguration = AppConfiguration::MappingConfiguration;

            if (json.isObject()) {
                Json::Value json_array;
                json_array.append(json);

                json = json_array;
            }

            if (!json.isArray()) {
                return false;
            }

            Json::ArrayIndex json_length = json.size();
            ppp::unordered_map<boost::asio::ip::tcp::endpoint, MappingConfiguration> tcp_mappings;
            ppp::unordered_map<boost::asio::ip::udp::endpoint, MappingConfiguration> udp_mappings;

            for (Json::ArrayIndex json_index = 0; json_index < json_length; json_index++) {
                Json::Value& jo = json[json_index];
                if (!jo.isObject()) {
                    continue;
                }

                MappingConfiguration mapping;
                mapping.protocol_tcp_or_udp = ToLower(LTrim(RTrim(JsonAuxiliary::AsString(jo["protocol"])))) != "udp";
                mapping.local_ip = JsonAuxiliary::AsString(jo["local-ip"]);
                mapping.local_port = JsonAuxiliary::AsValue<int>(jo["local-port"]);
                mapping.remote_ip = JsonAuxiliary::AsString(jo["remote-ip"]);
                mapping.remote_port = JsonAuxiliary::AsValue<int>(jo["remote-port"]);

                if (mapping.local_port <= IPEndPoint::MinPort || mapping.local_port > IPEndPoint::MaxPort) {
                    continue;
                }

                if (mapping.remote_port <= IPEndPoint::MinPort || mapping.remote_port > IPEndPoint::MaxPort) {
                    continue;
                }

                if (mapping.local_ip.empty() || mapping.remote_ip.empty()) {
                    continue;
                }

                boost::system::error_code ec;
                boost::asio::ip::address local_ip = StringToAddress(mapping.local_ip.data(), ec);
                if (ec) {
                    continue;
                }

                boost::asio::ip::address remote_ip = StringToAddress(mapping.remote_ip.data(), ec);
                if (ec) {
                    continue;
                }

                if (IPEndPoint::IsInvalid(local_ip)) {
                    continue;
                }

                if (!remote_ip.is_unspecified()) {
                    if (IPEndPoint::IsInvalid(remote_ip)) {
                        continue;
                    }
                }

                if (local_ip.is_multicast() || remote_ip.is_multicast()) {
                    continue;
                }

                mapping.local_ip = local_ip.to_string();
                mapping.remote_ip = remote_ip.to_string();

                if (mapping.protocol_tcp_or_udp) {
                    boost::asio::ip::tcp::endpoint remote_ep = boost::asio::ip::tcp::endpoint(remote_ip, mapping.remote_port);
                    tcp_mappings.emplace(remote_ep, mapping);
                }
                else {
                    boost::asio::ip::udp::endpoint remote_ep = boost::asio::ip::udp::endpoint(remote_ip, mapping.remote_port);
                    udp_mappings.emplace(remote_ep, mapping);
                }
            }

            ppp::vector<MappingConfiguration>& client_mappings = config.client.mappings;
            client_mappings.clear();

            for (auto&& [_, mapping] : tcp_mappings) {
                client_mappings.emplace_back(mapping);
            }

            for (auto&& [_, mapping] : udp_mappings) {
                client_mappings.emplace_back(mapping);
            }
            return true;
        }

        bool AppConfiguration::Loaded() noexcept {
            AppConfiguration& config = *this;
            if (config.concurrent < 1) {
                config.concurrent = Thread::GetProcessorCount();
            }

            config.server.node = std::max<int>(0, config.server.node);
            if (config.udp.dns.timeout < 1) {
                config.udp.dns.timeout = PPP_DEFAULT_DNS_TIMEOUT;
            }

            if (config.udp.inactive.timeout < 1) {
                config.udp.inactive.timeout = PPP_UDP_INACTIVE_TIMEOUT;
            }

            if (config.tcp.backlog < 1) {
                config.tcp.backlog = PPP_LISTEN_BACKLOG;
            }

            if (config.tcp.connect.timeout < 1) {
                config.tcp.connect.timeout = PPP_TCP_CONNECT_TIMEOUT;
            }

            if (config.tcp.inactive.timeout < 1) {
                config.tcp.inactive.timeout = PPP_TCP_INACTIVE_TIMEOUT;
            }

            LRTrim(config, 0);
            LRTrim(config, 1);

            if (config.client.guid.empty()) {
                config.client.guid = StringAuxiliary::Int128ToGuidString(MAKE_OWORD(UINT64_MAX, UINT64_MAX));
            }

            if (config.client.reconnections.timeout < 1) {
                config.client.reconnections.timeout = PPP_TCP_CONNECT_TIMEOUT;
            }

            int* pts[] = { &config.tcp.listen.port, &config.websocket.listen.ws, &config.websocket.listen.wss, &config.client.http_proxy.port };
            for (int i = 0; i < arraysizeof(pts); i++) {
                int& port = *pts[i];
                if (port < IPEndPoint::MinPort || port > IPEndPoint::MaxPort) {
                    port = IPEndPoint::MinPort;
                }
            }

            for (int i = 0; i < arraysizeof(config.cdn); i++) {
                int& cdn = config.cdn[i];
                if (cdn < IPEndPoint::MinPort || cdn > IPEndPoint::MaxPort) {
                    cdn = IPEndPoint::MinPort;
                }
            }

            ppp::string* ips[] = { &config.ip.public_, &config.ip.interface_, &config.client.http_proxy.bind };
            for (int i = 0; i < arraysizeof(ips); i++) {
                ppp::string& ip = *ips[i];
                if (ip.empty()) {
                    continue;
                }

                boost::system::error_code ec;
                boost::asio::ip::address address = StringToAddress(ip.data(), ec);
                if (ec) {
                    ip = "";
                }
                elif(IPEndPoint::IsInvalid(address) && !(address.is_unspecified() && (address.is_v4() || address.is_v6()))) {
                    ip = "";
                }
                else {
                    ip = Ipep::ToAddressString<ppp::string>(address);
                }
            }

            if (!Ciphertext::Support(config.key.protocol)) {
                config.key.protocol = PPP_DEFAULT_KEY_PROTOCOL;
            }

            if (!Ciphertext::Support(config.key.transport)) {
                config.key.transport = PPP_DEFAULT_KEY_TRANSPORT;
            }

            if (config.key.protocol_key.empty()) {
                config.key.protocol_key = BOOST_BEAST_VERSION_STRING;
            }

            if (config.key.transport_key.empty()) {
                config.key.transport_key = BOOST_BEAST_VERSION_STRING;
            }

            if (!Ipep::IsDomainAddress(config.websocket.host) || config.websocket.path.empty() || config.websocket.path[0] != '/') {
                config.websocket.listen.ws = IPEndPoint::MinPort;
                config.websocket.listen.wss = IPEndPoint::MinPort;
            }
            elif(!ppp::ssl::SSL::VerifySslCertificate(config.websocket.ssl.certificate_file, config.websocket.ssl.certificate_key_file, config.websocket.ssl.certificate_chain_file)) {
                config.websocket.listen.wss = IPEndPoint::MinPort;
            }

            if (config.websocket.listen.wss == IPEndPoint::MinPort) {
                config.websocket.ssl.certificate_file = "";
                config.websocket.ssl.certificate_key_file = "";
                config.websocket.ssl.certificate_chain_file = "";
                config.websocket.ssl.certificate_key_password = "";
            }
            elif(config.websocket.ssl.ciphersuites.empty()) {
                config.websocket.ssl.ciphersuites = GetDefaultCipherSuites();
            } 

            if (config.websocket.listen.ws == IPEndPoint::MinPort) {
                config.websocket.path = "";
                config.websocket.host = "";
                config.websocket.http.error = "";
                config.websocket.http.request.clear();
                config.websocket.http.response.clear();
            }

            if (ips) {
                int destinationPort = IPEndPoint::MinPort;
                ppp::string destinationIP;

                ppp::string& redirect_string = config.udp.dns.redirect;
                if (!Ipep::ParseEndPoint(redirect_string, destinationIP, destinationPort)) {
                    redirect_string = "";
                }
                else {
                    boost::system::error_code ec;
                    boost::asio::ip::address address = StringToAddress(destinationIP.data(), ec);
                    if (ec) {
                        if (!Ipep::IsDomainAddress(destinationIP)) {
                            redirect_string = "";
                        }
                    }
                    elif(IPEndPoint::IsInvalid(address)) {
                        redirect_string = "";
                    }
                }
            }

            if (config.vmem.path.empty() || config.vmem.size < 1) {
                config.vmem.size = 0;
                config.vmem.path = "";
            }

            ppp::string& log = config.server.log;
            if (log.size() > 0) {
                log = File::GetFullPath(File::RewritePath(log.data()).data());
            }

            config.client.bandwidth = std::max<int64_t>(0, config.client.bandwidth);
            return true;
        }

        bool AppConfiguration::Load(const ppp::string& path) noexcept {
            Clear();
            if (path.empty()) {
                return false;
            }

            ppp::string file_path = File::GetFullPath(File::RewritePath(path.data()).data());
            if (file_path.empty()) {
                return false;
            }

            ppp::string json_string = File::ReadAllText(path.data());
            if (json_string.empty()) {
                return false;
            }

            Json::Value json = JsonAuxiliary::FromString(json_string);
            if (!json.isObject()) {
                return false;
            }
            else {
                return Load(json);
            }
        }

        template <typename TMap>
        static bool ReadJsonAllTokensToMap(const Json::Value& json, TMap& map) noexcept {
            map.clear();

            if (json.isObject()) {
                for (ppp::string& k : json.getMemberNames()) {
                    Json::Value v = json[k.data()];
                    map[k] = LTrim(RTrim(JsonAuxiliary::AsString(v)));
                }

                return true;
            }
            elif(json.isArray()) {
                Json::ArrayIndex json_size = json.size();
                for (Json::ArrayIndex json_index = 0; json_index < json_size; json_index++) {
                    Json::Value v = json[json_index];
                    map[stl::to_string<ppp::string>(json_index)] = LTrim(RTrim(JsonAuxiliary::AsString(v)));
                }

                return true;
            }

            return false;
        }

        /*
         * Author: Binjie09 (AI Assistant)
         *
         * Description: This code is generated by Binjie09, an AI assistant.
         *              It is designed to read JSON data into the C++ data structure AppConfiguration
         *              using the Jsoncpp library's Json::Value and JsonAuxiliary::AsValue<TValue> function.
         *
         * Date: 2023-06-28
         */
        bool AppConfiguration::Load(Json::Value& json) noexcept {
            Clear();
            if (!json.isObject()) {
                return false;
            }

            AppConfiguration& config = *this;
            config.concurrent = JsonAuxiliary::AsValue<int>(json["concurrent"]);
            config.cdn[0] = JsonAuxiliary::AsValue<int>(json["cdn"][0]);
            config.cdn[1] = JsonAuxiliary::AsValue<int>(json["cdn"][1]);

            config.ip.public_ = JsonAuxiliary::AsValue<ppp::string>(json["ip"]["public"]);
            config.ip.interface_ = JsonAuxiliary::AsValue<ppp::string>(json["ip"]["interface"]);

            config.vmem.size = JsonAuxiliary::AsValue<int64_t>(json["vmem"]["size"]);
            config.vmem.path = JsonAuxiliary::AsValue<ppp::string>(json["vmem"]["path"]);

            config.udp.inactive.timeout = JsonAuxiliary::AsValue<int>(json["udp"]["inactive"]["timeout"]);
            config.udp.dns.timeout = JsonAuxiliary::AsValue<int>(json["udp"]["dns"]["timeout"]);
            config.udp.dns.redirect = JsonAuxiliary::AsValue<ppp::string>(json["udp"]["dns"]["redirect"]);

            config.tcp.inactive.timeout = JsonAuxiliary::AsValue<int>(json["tcp"]["inactive"]["timeout"]);
            config.tcp.connect.timeout = JsonAuxiliary::AsValue<int>(json["tcp"]["connect"]["timeout"]);
            config.tcp.listen.port = JsonAuxiliary::AsValue<int>(json["tcp"]["listen"]["port"]);
            config.tcp.turbo = JsonAuxiliary::AsValue<bool>(json["tcp"]["turbo"]);
            config.tcp.backlog = JsonAuxiliary::AsValue<int>(json["tcp"]["backlog"]);
            config.tcp.fast_open = JsonAuxiliary::AsValue<bool>(json["tcp"]["fast-open"]);

            config.websocket.listen.ws = JsonAuxiliary::AsValue<int>(json["websocket"]["listen"]["ws"]);
            config.websocket.listen.wss = JsonAuxiliary::AsValue<int>(json["websocket"]["listen"]["wss"]);
            config.websocket.ssl.certificate_file = JsonAuxiliary::AsValue<std::string>(json["websocket"]["ssl"]["certificate-file"]);
            config.websocket.ssl.certificate_key_file = JsonAuxiliary::AsValue<std::string>(json["websocket"]["ssl"]["certificate-key-file"]);
            config.websocket.ssl.certificate_chain_file = JsonAuxiliary::AsValue<std::string>(json["websocket"]["ssl"]["certificate-chain-file"]);
            config.websocket.ssl.certificate_key_password = JsonAuxiliary::AsValue<std::string>(json["websocket"]["ssl"]["certificate-key-password"]);
            config.websocket.ssl.ciphersuites = JsonAuxiliary::AsValue<std::string>(json["websocket"]["ssl"]["ciphersuites"]);
            config.websocket.ssl.verify_peer = JsonAuxiliary::AsValue<bool>(json["websocket"]["ssl"]["verify-peer"]);
            config.websocket.host = JsonAuxiliary::AsValue<ppp::string>(json["websocket"]["host"]);
            config.websocket.path = JsonAuxiliary::AsValue<ppp::string>(json["websocket"]["path"]);
            config.websocket.http.error = JsonAuxiliary::AsValue<ppp::string>(json["websocket"]["http"]["error"]);
            ReadJsonAllTokensToMap(json["websocket"]["http"]["request"], config.websocket.http.request);
            ReadJsonAllTokensToMap(json["websocket"]["http"]["response"], config.websocket.http.response);

            config.key.kf = JsonAuxiliary::AsValue<int>(json["key"]["kf"]);
            config.key.kl = JsonAuxiliary::AsValue<int>(json["key"]["kl"]);
            config.key.kh = JsonAuxiliary::AsValue<int>(json["key"]["kh"]);
            config.key.kx = JsonAuxiliary::AsValue<int>(json["key"]["kx"]);
            config.key.protocol = JsonAuxiliary::AsValue<ppp::string>(json["key"]["protocol"]);
            config.key.protocol_key = JsonAuxiliary::AsValue<ppp::string>(json["key"]["protocol-key"]);
            config.key.transport = JsonAuxiliary::AsValue<ppp::string>(json["key"]["transport"]);
            config.key.transport_key = JsonAuxiliary::AsValue<ppp::string>(json["key"]["transport-key"]);
            config.key.masked = JsonAuxiliary::AsValue<bool>(json["key"]["masked"]);
            config.key.plaintext = JsonAuxiliary::AsValue<bool>(json["key"]["plaintext"]);
            config.key.delta_encode = JsonAuxiliary::AsValue<bool>(json["key"]["delta-encode"]);
            config.key.shuffle_data = JsonAuxiliary::AsValue<bool>(json["key"]["shuffle-data"]);

            config.server.log = JsonAuxiliary::AsValue<ppp::string>(json["server"]["log"]);
            config.server.node = JsonAuxiliary::AsValue<int>(json["server"]["node"]);
            config.server.subnet = JsonAuxiliary::AsValue<bool>(json["server"]["subnet"]);
            config.server.mapping = JsonAuxiliary::AsValue<bool>(json["server"]["mapping"]);
            config.server.backend = JsonAuxiliary::AsValue<ppp::string>(json["server"]["backend"]);
            config.server.backend_key = JsonAuxiliary::AsValue<ppp::string>(json["server"]["backend-key"]);

            LoadAllMappings(config, json["client"]["mappings"]);
            config.client.reconnections.timeout = JsonAuxiliary::AsValue<int>(json["client"]["reconnections"]["timeout"]);
            config.client.guid = JsonAuxiliary::AsValue<ppp::string>(json["client"]["guid"]);
            config.client.server = JsonAuxiliary::AsValue<ppp::string>(json["client"]["server"]);
            config.client.bandwidth = JsonAuxiliary::AsValue<int64_t>(json["client"]["bandwidth"]);
            config.client.http_proxy.port = JsonAuxiliary::AsValue<int>(json["client"]["http-proxy"]["port"]);
            config.client.http_proxy.bind = JsonAuxiliary::AsValue<ppp::string>(json["client"]["http-proxy"]["bind"]);
#if defined(_WIN32)
            config.client.paper_airplane.tcp = JsonAuxiliary::AsValue<bool>(json["client"]["paper-airplane"]["tcp"]);
#endif
            return Loaded();
        }

        /*
         * Author: Binjie09 (AI Assistant)
         *
         * Description: This code is generated by Binjie09, an AI assistant.
         *              Convert AppConfiguration object to Json::Value object.
         *
         * Date: 2023-06-28
         */
        Json::Value AppConfiguration::ToJson() noexcept {
            Json::Value root;
            AppConfiguration& config = *this;

            // Set concurrent
            root["concurrent"] = config.concurrent;

            // Set cdn array
            Json::Value cdn(Json::arrayValue);
            cdn.append(config.cdn[0]);
            cdn.append(config.cdn[1]);
            root["cdn"] = cdn;

            // Set ip structure
            Json::Value ip;
            ip["public"] = config.ip.public_;
            ip["interface"] = config.ip.interface_;
            root["ip"] = ip;

            // Set vmem structure
            Json::Value vmem;
            vmem["size"] = config.vmem.size;
            vmem["path"] = config.vmem.path;
            root["vmem"] = vmem;

            // Set udp structure
            Json::Value udp;
            udp["inactive"]["timeout"] = config.udp.inactive.timeout;
            udp["dns"]["timeout"] = config.udp.dns.timeout;
            udp["dns"]["redirect"] = config.udp.dns.redirect;
            root["udp"] = udp;

            // Set tcp structure
            Json::Value tcp;
            tcp["inactive"]["timeout"] = config.tcp.inactive.timeout;
            tcp["connect"]["timeout"] = config.tcp.connect.timeout;
            tcp["listen"]["port"] = config.tcp.listen.port;
            tcp["turbo"] = config.tcp.turbo;
            tcp["backlog"] = config.tcp.backlog;
            tcp["fast-open"] = config.tcp.fast_open;
            root["tcp"] = tcp;

            // Set websocket structure
            Json::Value websocket;
            websocket["listen"]["ws"] = config.websocket.listen.ws;
            websocket["listen"]["wss"] = config.websocket.listen.wss;
            websocket["ssl"]["certificate-file"] = stl::transform<ppp::string>(config.websocket.ssl.certificate_file);
            websocket["ssl"]["certificate-key-file"] = stl::transform<ppp::string>(config.websocket.ssl.certificate_key_file);
            websocket["ssl"]["certificate-chain-file"] = stl::transform<ppp::string>(config.websocket.ssl.certificate_chain_file);
            websocket["ssl"]["certificate-key-password"] = stl::transform<ppp::string>(config.websocket.ssl.certificate_key_password);
            websocket["ssl"]["ciphersuites"] = stl::transform<ppp::string>(config.websocket.ssl.ciphersuites);
            websocket["ssl"]["verify-peer"] = config.websocket.ssl.verify_peer;
            websocket["http"]["error"] = stl::transform<ppp::string>(config.websocket.http.error);

            Json::Value& request = websocket["http"]["request"];
            for (auto&& [k, v] : config.websocket.http.request) {
                request[k.data()] = stl::transform<ppp::string>(v);
            }

            Json::Value& response = websocket["http"]["response"];
            for (auto&& [k, v] : config.websocket.http.response) {
                response[k.data()] = stl::transform<ppp::string>(v);
            }

            websocket["host"] = config.websocket.host;
            websocket["path"] = config.websocket.path;
            root["websocket"] = websocket;

            // Set key structure
            Json::Value key;
            key["kf"] = config.key.kf;
            key["kl"] = config.key.kl;
            key["kh"] = config.key.kh;
            key["kx"] = config.key.kx;
            key["protocol"] = config.key.protocol;
            key["protocol-key"] = config.key.protocol_key;
            key["transport"] = config.key.transport;
            key["transport-key"] = config.key.transport_key;
            key["masked"] = config.key.masked;
            key["plaintext"] = config.key.plaintext;
            key["delta-encode"] = config.key.delta_encode;
            key["shuffle-data"] = config.key.shuffle_data;
            root["key"] = key;

            // Set server structure
            Json::Value server;
            server["log"] = config.server.log;
            server["node"] = config.server.node;
            server["subnet"] = config.server.subnet;
            server["mapping"] = config.server.mapping;
            server["backend"] = config.server.backend; /* ws://192.168.0.24/ppp/webhook */
            server["backend-key"] = config.server.backend_key;
            root["server"] = server;

            // Set client structure
            Json::Value client;
            Json::Value& mappings = client["mappings"];
            for (MappingConfiguration& mapping : config.client.mappings) {
                Json::Value jo;
                jo["protocol"] = mapping.protocol_tcp_or_udp ? "tcp" : "udp";
                jo["local-ip"] = mapping.local_ip;
                jo["local-port"] = mapping.local_port;
                jo["remote-ip"] = mapping.remote_ip;
                jo["remote-port"] = mapping.remote_port;
                mappings.append(jo);
            }

            client["http-proxy"]["bind"] = config.client.http_proxy.bind;
            client["http-proxy"]["port"] = config.client.http_proxy.port;
            client["reconnections"]["timeout"] = config.client.reconnections.timeout;
            client["guid"] = config.client.guid;
            client["server"] = config.client.server;
            client["bandwidth"] = config.client.bandwidth;
#if defined(_WIN32)
            client["paper-airplane"]["tcp"] = config.client.paper_airplane.tcp;
#endif

            root["client"] = client;

            return root;
        }

        /*
         * Author: Binjie09 (AI Assistant)
         *
         * Description: This code is generated by Binjie09, an AI assistant.
         *              Convert AppConfiguration object to json string.
         *
         * Date: 2023-06-28
         */
        ppp::string AppConfiguration::ToString() noexcept {
            Json::Value json = ToJson();
            return JsonAuxiliary::ToString(json);
        }
    }
}