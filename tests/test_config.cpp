#include <cassert>
#include <chrono>
#include <filesystem>
#include <list>
#include <map>
#include <unordered_map>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

#include <pans/config/config.h>

namespace config_test
{
namespace
{
struct Person
{
    std::string m_name;
    int m_age;
    bool m_sex;

    bool operator==(const Person&) const = default;
};
}
}

namespace pans
{
template<>
class ConfigConvert<config_test::Person>
{
public:
    [[nodiscard]] static config_test::Person FromNode(const YAML::Node& node)
    {
        return {
            .m_name = node["name"].as<std::string>(),
            .m_age  = node["age"].as<int>(),
            .m_sex  = node["sex"].as<bool>()
        };
    }

    [[nodiscard]] static YAML::Node ToNode(const config_test::Person& person)
    {
        YAML::Node node(YAML::NodeType::Map);
        node["name"] = person.m_name;
        node["age"] = person.m_age;
        node["sex"] = person.m_sex;
        return node;
    }
};
}

int main(int argc, char* argv[])
{
    const std::filesystem::path config_directory = argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path("../../etc");
    const std::string config_absolute_directory = std::filesystem::absolute(config_directory).lexically_normal().string();

    auto port = pans::Config::Lookup("test.port", 8080, "test port");
    auto int_vector = pans::Config::Lookup("test.int_vec", std::vector<int>{1, 2, 3}, "test int vector");
    auto int_list = pans::Config::Lookup("test.int_list", std::list<int>{1, 2, 3}, "test int list");
    auto int_set = pans::Config::Lookup("test.int_set", std::set<int>{1, 2, 3}, "test int set");
    auto int_unordered_set = pans::Config::Lookup("test.int_unordered_set", std::unordered_set<int>{}, "test int unordered set");
    auto string_int_map = pans::Config::Lookup("test.str_int_map", std::map<std::string, int>{}, "test string-int map");
    auto string_int_unordered_map = pans::Config::Lookup("test.str_int_umap", std::unordered_map<std::string, int>{}, "test string-int umap");
    auto person = pans::Config::Lookup("class.person", config_test::Person{}, "test person");
    auto person_map = pans::Config::Lookup("class.map", std::map<std::string, config_test::Person>{}, "test person map");
    auto logs = pans::Config::Lookup("logs", YAML::Node(YAML::NodeType::Sequence), "logger definitations");

    assert(port &&
        int_vector &&
        int_list &&
        int_set &&
        int_unordered_set &&
        string_int_map &&
        string_int_unordered_map &&
        person &&
        person_map &&
        logs);

    int port_change_count{0};
    const pans::ListenerId port_listener_id = port->addListener(
        [&port_change_count](const int& old_value, const int& new_value)
            {
                assert(old_value == 8080);
                assert(new_value == 5005);
                ++port_change_count;
            }
        );

    int logs_change_count{0};
    const pans::ListenerId logs_listener_id = logs->addListener(
        [&logs_change_count](const auto&, const auto&)
            {
                ++logs_change_count;
            }
        );
    assert(logs_listener_id != 0);

    pans::Config::LoadFromConfDir(config_absolute_directory);

    assert(port->getValue() == 5005);
    assert(port_change_count == 1);
    assert(logs_change_count == 1);;
    assert(int_vector->getValue() == std::vector<int>({10, 20, 30}));
    assert(int_list->getValue() == std::list<int>({40, 50, 60, 40}));
    assert(int_set->getValue() == std::set<int>({20,30, 40}));
    assert(int_unordered_set->getValue() == std::unordered_set<int>({20, 30, 40}));
    assert((string_int_map->getValue() == std::map<std::string, int>({{"k", 30}, {"k1", 20}, {"k2", 40}})));
    assert((string_int_unordered_map->getValue() == std::unordered_map<std::string, int>({{"k", 30}, {"k1", 20}, {"k2", 40}})));

    const config_test::Person expected_person{
        .m_name = "norym",
        .m_age  = 20,
        .m_sex  = true
    };
    assert(person->getValue() == expected_person);

    const auto persons = person_map->getValue();
    assert(persons.size() == 2);
    assert(persons.at("pans01") == config_test::Person({"arrym", 21, false}));
    assert(persons.at("pans02") == config_test::Person({"aorym", 0, false}));

    const YAML::Node log_defs = logs->getValue();
    assert(log_defs.size() == 2);
    assert(log_defs[0]["name"].as<std::string>() == "root");
    assert(log_defs[0]["level"].as<std::string>() == "debug");
    assert(log_defs[0]["appenders"].IsSequence());
    assert(log_defs[0]["appenders"].size() == 2);
    assert(log_defs[0]["appenders"][0]["type"].as<std::string>() == "FileLogAppender");
    assert(log_defs[0]["appenders"][0]["file"].as<std::string>() == "log.txt");
    assert(log_defs[0]["appenders"][1]["type"].as<std::string>() == "StdoutLogAppender");
    assert(log_defs[1]["name"].as<std::string>() == "system");

    pans::Config::LoadFromConfDir(config_absolute_directory);
    assert(port_change_count == 1);
    assert(logs_change_count == 1);

    pans::Config::LoadFromConfDir(config_absolute_directory, true);
    assert(port_change_count == 1);
    assert(logs_change_count == 2);

    port->removeLisitener(port_listener_id);
    logs->removeLisitener(logs_listener_id);
    assert(port->listenerCount() == 0);
    assert(logs->listenerCount() == 0);

    port->setValue(4000);
    assert(port_change_count == 1);
}
