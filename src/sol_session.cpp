#include "sol_session.hpp"

#include "http_client.hpp"
#include "text.hpp"
#include "url.hpp"

#include <boost/json.hpp>

#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace hisol {
namespace json = boost::json;
namespace boost_system = boost::system;

namespace {

json::object parse_json_object(std::string_view body, std::string_view context)
{
    boost_system::error_code error;
    json::value value = json::parse(body, error);
    if (error) {
        throw std::runtime_error(std::string(context) + " did not return JSON: " + error.message());
    }

    const auto* object = value.if_object();
    if (object == nullptr) {
        throw std::runtime_error(std::string(context) + " returned JSON, but not an object");
    }

    return *object;
}

std::optional<std::string> json_string_field(const json::object& object, std::string_view key)
{
    const auto* value = object.if_contains(key);
    if (value == nullptr) {
        return std::nullopt;
    }
    if (const auto* string_value = value->if_string()) {
        return std::string(*string_value);
    }
    return json::serialize(*value);
}

std::string login_to_bmc(const SolOptions& options, CookieJar& cookies)
{
    std::string body = "username=" + form_url_encode(options.username);
    body += "&password=" + form_url_encode(options.password);

    auto response = https_request(
        options.base_url,
        options.insecure,
        http::verb::post,
        "/api/session",
        std::move(body),
        "application/x-www-form-urlencoded; charset=UTF-8",
        &cookies,
        {},
        options.verbose);

    require_success_status(response, "/api/session");
    const std::string response_body = decode_response_body(response);
    const json::object session = parse_json_object(response_body, "/api/session");

    if (const auto tfa_enabled = json_string_field(session, "TFAEnabled");
        tfa_enabled.has_value() && *tfa_enabled == "1") {
        const auto tfa_status = json_string_field(session, "TFAStatus").value_or("0");
        if (tfa_status != "1") {
            throw std::runtime_error("login requires two-factor authentication; this prototype does not handle TFA yet");
        }
    }

    const auto csrf_token = json_string_field(session, "CSRFToken");
    if (!csrf_token.has_value() || csrf_token->empty()) {
        throw std::runtime_error("/api/session response did not contain CSRFToken: " + body_snippet(response_body));
    }

    cookies.set("__Host-garc", *csrf_token);
    return *csrf_token;
}

void prime_sol_session(const SolOptions& options, CookieJar& cookies, const std::string& csrf_token)
{
    const std::vector<Header> headers{
        Header{http::field::origin, {}, make_origin(options.base_url)},
        Header{http::field::referer, {}, make_origin(options.base_url) + "/#remote_control"},
        Header{http::field::unknown, "X-CSRFTOKEN", csrf_token},
    };

    auto response = https_request(
        options.base_url,
        options.insecure,
        http::verb::post,
        "/api/sol/solcfg",
        {},
        "application/x-www-form-urlencoded; charset=UTF-8",
        &cookies,
        headers,
        options.verbose);

    require_success_status(response, "/api/sol/solcfg");
}

} // namespace

SolSession open_sol_session(const SolOptions& options)
{
    CookieJar cookies;
    std::string csrf_token = login_to_bmc(options, cookies);
    prime_sol_session(options, cookies, csrf_token);
    return SolSession{std::move(cookies), std::move(csrf_token)};
}

} // namespace hisol
