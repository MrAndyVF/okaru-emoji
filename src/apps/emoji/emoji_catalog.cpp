#include "apps/emoji/emoji_catalog.h"

#include "util/file_utils.h"
#include "util/string_utils.h"

#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>
#include <random>
#include <ranges>
#include <toml++/toml.hpp>

namespace okaru::emoji {

  namespace {

    void addCategory(Catalog& catalog, const std::string& category) {
      if (std::ranges::find(catalog.categories, category) == catalog.categories.end()) {
        catalog.categories.push_back(category);
      }
    }

    nlohmann::json readJson(const std::filesystem::path& path) {
      std::ifstream file(path);
      if (!file.is_open()) {
        return nullptr;
      }
      return nlohmann::json::parse(file, nullptr, false);
    }

    // Longitud en bytes del primer carácter UTF-8.
    std::size_t firstCharLength(std::string_view text) {
      if (text.empty()) {
        return 0;
      }
      const auto lead = static_cast<unsigned char>(text[0]);
      const std::size_t length = lead < 0x80 ? 1 : lead < 0xE0 ? 2 : lead < 0xF0 ? 3 : 4;
      return std::min(length, text.size());
    }

    std::string randomId() {
      std::random_device device;
      std::uniform_int_distribution<int> digit(0, 15);
      std::string id;
      for (int i = 0; i < 32; ++i) {
        id.push_back("0123456789abcdef"[digit(device)]);
      }
      return id;
    }

  } // namespace

  Catalog loadEmoji(const std::filesystem::path& path) {
    Catalog catalog;
    const nlohmann::json json = readJson(path);
    if (!json.is_array()) {
      return catalog;
    }
    catalog.items.reserve(json.size());
    for (const auto& entry : json) {
      if (!entry.is_object()) {
        continue;
      }
      Item item{
          .text = entry.value("emoji", ""),
          .name = entry.value("name", ""),
          .category = entry.value("category", ""),
          .tone = entry.value("tone", false),
      };
      if (item.text.empty()) {
        continue;
      }
      item.search = StringUtils::toLower(item.name);
      if (const auto keywords = entry.find("keywords"); keywords != entry.end() && keywords->is_array()) {
        for (const auto& keyword : *keywords) {
          if (keyword.is_string()) {
            item.search += ' ' + StringUtils::toLower(keyword.get<std::string>());
          }
        }
      }
      addCategory(catalog, item.category);
      catalog.items.push_back(std::move(item));
    }
    return catalog;
  }

  Catalog loadGrouped(const std::filesystem::path& path) {
    Catalog catalog;
    const nlohmann::json json = readJson(path);
    if (!json.is_array()) {
      return catalog;
    }
    for (const auto& group : json) {
      if (!group.is_object() || !group.contains("items") || !group["items"].is_array()) {
        continue;
      }
      const std::string category = group.value("category", "");
      addCategory(catalog, category);
      for (const auto& entry : group["items"]) {
        if (!entry.is_object()) {
          continue;
        }
        Item item{.text = entry.value("text", ""), .category = category};
        if (item.text.empty()) {
          continue;
        }
        item.search = StringUtils::toLower(entry.value("keywords", "") + ' ' + category);
        catalog.items.push_back(std::move(item));
      }
    }
    return catalog;
  }

  std::vector<std::size_t> filter(const Catalog& catalog, std::string_view query, std::string_view category) {
    std::vector<std::string> words;
    for (const auto word : std::views::split(StringUtils::toLower(query), ' ')) {
      if (!word.empty()) {
        words.emplace_back(word.begin(), word.end());
      }
    }
    std::vector<std::size_t> out;
    for (std::size_t i = 0; i < catalog.items.size(); ++i) {
      const Item& item = catalog.items[i];
      if (words.empty()) {
        if (category.empty() || item.category == category) {
          out.push_back(i);
        }
        continue;
      }
      if (std::ranges::all_of(words, [&](const std::string& word) {
            return item.search.contains(word) || item.text.contains(word);
          })) {
        out.push_back(i);
      }
    }
    return out;
  }

  std::string withTone(std::string_view emoji, int tone) {
    if (tone < 1 || tone > 5) {
      return std::string(emoji);
    }
    const std::size_t head = firstCharLength(emoji);
    std::string_view rest = emoji.substr(head);
    constexpr std::string_view kVariationSelector = "\xEF\xB8\x8F"; // U+FE0F
    if (rest.starts_with(kVariationSelector)) {
      rest.remove_prefix(kVariationSelector.size());
    }
    // U+1F3FB..U+1F3FF en UTF-8: F0 9F 8F BB..BF
    const std::string modifier{'\xF0', '\x9F', '\x8F', static_cast<char>(0xBA + tone)};
    return std::string(emoji.substr(0, head)) + modifier + std::string(rest);
  }

  void addRecent(State& state, std::string text) {
    std::erase(state.recents, text);
    state.recents.insert(state.recents.begin(), std::move(text));
    if (state.recents.size() > kMaxRecents) {
      state.recents.resize(kMaxRecents);
    }
  }

  std::filesystem::path statePath() { return std::filesystem::path(FileUtils::stateDir()) / "emoji.toml"; }

  State loadState(const std::filesystem::path& path) {
    State state;
    try {
      const toml::table root = toml::parse_file(path.string());
      if (const auto* recents = root["recents"].as_array(); recents != nullptr) {
        for (const auto& entry : *recents) {
          if (const auto text = entry.value<std::string>(); text.has_value() && !text->empty()) {
            state.recents.push_back(*text);
          }
        }
      }
      if (state.recents.size() > kMaxRecents) {
        state.recents.resize(kMaxRecents);
      }
      state.tone = std::clamp<int>(root["tone"].value_or(0), 0, 5);
      state.tab = static_cast<Tab>(std::clamp<int>(root["tab"].value_or(0), 0, 3));
      state.customerId = root["customer_id"].value_or(std::string{});
    } catch (const toml::parse_error&) {
      // Primer arranque o archivo dañado: valores de fábrica.
    }
    if (state.customerId.empty()) {
      state.customerId = randomId();
    }
    return state;
  }

  bool saveState(const State& state, const std::filesystem::path& path) {
    toml::array recents;
    for (const auto& text : state.recents) {
      recents.push_back(text);
    }
    toml::table root{
        {"recents", std::move(recents)},
        {"tone", state.tone},
        {"tab", static_cast<int>(state.tab)},
        {"customer_id", state.customerId},
    };
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    const std::filesystem::path temp = path.string() + ".tmp";
    {
      std::ofstream file(temp, std::ios::trunc);
      if (!file) {
        return false;
      }
      file << root << '\n';
      if (!file) {
        return false;
      }
    }
    std::filesystem::rename(temp, path, ec);
    return !ec;
  }

} // namespace okaru::emoji
