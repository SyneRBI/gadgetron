#include "Python.h"

#include <list>
#include "ProcessHelper.h"

#include "connection/config/Config.h"

#include <boost/asio.hpp>

#include "log.h"
#include <regex>

namespace Gadgetron::Server::Connection::Nodes {

    using namespace Gadgetron::Core;

    namespace {

        std::tuple<int, int, int> parse_version(const std::string& versionstring) {

            auto version_rules = std::regex(R"(.*?([0-9]+)\.([0-9]+)\.([0-9]+))");
            std::smatch matches;
            auto found = std::regex_search(versionstring, matches, version_rules);

            if (!found)
                return {0, 0, 0};

            int major_version = std::stoi(matches[1].str());
            int minor_version = std::stoi(matches[2].str());
            int patch_version = std::stoi(matches[3].str());

            return {major_version, minor_version, patch_version};
        }

        bool is_valid_python3(const std::string& pythonname){
        try {
            auto output = Process::capture_output(pythonname + " --version 2>&1");

            auto [major, minor, patch] = parse_version(output);

            if ((major == 3) && (minor > 5))
                return true;


       }
        catch (...){
            return false;
        }
       return false;


    };

    std::string get_python_executable()  {
        auto possible_names = std::vector<std::string>{"python", "python3"};
        for (auto &name : possible_names) {
            if (is_valid_python3(name)) {
                return name;
            }
        }
        throw std::runtime_error("Could not find valid python installation");
    }

    }

    Gadgetron::Process::child start_python_module(
        const Config::Execute &execute,
        unsigned short port,
        const StreamContext &context
    ) {
        auto python_path = (context.paths.gadgetron_home / "share" / "gadgetron" / "python").string();

        std::list<std::string> args{
            "-m", "gadgetron",
            std::to_string(port),
            execute.name
        };

        if(execute.target) args.push_back(execute.target.value());
        
        std::vector<std::pair<std::string, std::string>> env_overrides;
        auto orig_python_path = std::getenv("PYTHONPATH");
        if (orig_python_path)
            env_overrides.emplace_back("PYTHONPATH", std::string(orig_python_path) + ":" + python_path);
        else
            env_overrides.emplace_back("PYTHONPATH", python_path);
        env_overrides.emplace_back("GADGETRON_STORAGE_ADDRESS", context.storage_address);

        auto module = Process::child(
                Process::search_path(get_python_executable()),
                std::vector<std::string>(args.begin(), args.end()),
                env_overrides
        );

        GINFO_STREAM("Started external Python module (pid: " << module.id() << ").");
        return std::move(module);
    }

    bool python_available() noexcept {
        try {
            return std::system(("\"" + get_python_executable() + "\" -m gadgetron >/dev/null 2>&1").c_str()) == 0;
        }
        catch (...) {
            return false;
        }
    }
}
