#include "cli.hpp"

#include "app_info.hpp"
#include "commands.hpp"
#include "console.hpp"
#include "options.hpp"
#include "text.hpp"
#include "url.hpp"

#include <CLI/CLI.hpp>

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace hisol {
namespace {

void fill_default_sol_credentials(SolOptions& options, const std::string& password_env_name)
{
    if (options.username.empty()) {
        options.username = read_env("HISOL_USERNAME");
    }

    if (options.password.empty() && !password_env_name.empty()) {
        options.password = read_env(password_env_name);
        if (options.password.empty()) {
            throw std::invalid_argument(
                "password environment variable is empty or not set: " + password_env_name);
        }
    }

    if (options.password.empty()) {
        options.password = read_env("HISOL_PASSWORD");
    }

    if (options.username.empty()) {
        throw std::invalid_argument("missing username; pass --username or set HISOL_USERNAME");
    }
    if (options.password.empty()) {
        options.password = read_password_from_console("Password for " + options.username + ": ");
    }
    if (options.password.empty()) {
        throw std::invalid_argument("empty password entered");
    }
}

void configure_megarac_subcommand(CLI::App& command, SolOptions& options, std::string& password_env_name)
{
    command.add_flag("-k,--insecure", options.insecure, "Disable certificate and hostname verification.");
    command.add_flag("-v,--verbose", options.verbose, "Log setup HTTP and WebSocket handshake details to stderr.");
    command.add_flag("--debug-frames", options.debug_frames, "Log WebSocket frame sizes to stderr.");
    command.add_flag("--raw", options.raw, "Use an exact stdin/stdout byte bridge with no console handling.");
    command.add_option("-u,--username", options.username, "Username for the MegaRAC /api/session login.");
    command.add_option("-p,--password", options.password, "Password for the MegaRAC /api/session login.");
    command.add_option("--password-env", password_env_name, "Read the password from an environment variable.");
}

} // namespace

int run_cli(int argc, char* argv[])
{
    CLI::App app{std::string(kExpansion) + " MegaRAC stream bridge", std::string(kName)};
    app.set_version_flag("--version", std::string(kName) + " " + std::string(kVersion));
    app.require_subcommand(1);

    GetOptions get_options;
    std::string get_url;
    CLI::App* get = app.add_subcommand("get", "Fetch an HTTPS URL and write the response body to stdout.");
    get->add_flag("-i,--include", get_options.include_headers, "Write HTTP status and headers before the body.");
    get->add_flag("-k,--insecure", get_options.insecure, "Disable certificate and hostname verification.");
    get->add_flag("-v,--verbose", get_options.verbose, "Log HTTP request/response details to stderr.");
    get->add_option("url", get_url, "https://host[:port][/path]")->required();

    SolOptions sol_options;
    std::string sol_url;
    std::string password_env_name;
    CLI::App* sol = app.add_subcommand("sol", "Connect to a MegaRAC HTTPS WebSocket SOL endpoint.");
    configure_megarac_subcommand(*sol, sol_options, password_env_name);
    sol->add_option("url", sol_url, "https://host[:port]")->required();
    CLI::App* megarac = app.add_subcommand("megarac", "Connect to a MegaRAC HTTPS WebSocket SOL endpoint.");
    configure_megarac_subcommand(*megarac, sol_options, password_env_name);
    megarac->add_option("url", sol_url, "https://host[:port]")->required();

    if (argc == 1) {
        std::cout << app.help();
        return EXIT_SUCCESS;
    }

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError& error) {
        return app.exit(error);
    }

    if (*get) {
        get_options.url = parse_https_url(get_url);
        return run_get(get_options);
    }

    if (*sol || *megarac) {
        sol_options.base_url = parse_https_url(sol_url);
        sol_options.base_url.target = "/";
        fill_default_sol_credentials(sol_options, password_env_name);
        return run_sol(sol_options);
    }

    return EXIT_SUCCESS;
}

} // namespace hisol
