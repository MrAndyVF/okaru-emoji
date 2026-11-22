#include "apps/emoji/klipy.h"

#include "util/string_utils.h"

#include <cctype>
#include <format>
#include <nlohmann/json.hpp>

namespace okaru::emoji {

  namespace {

    // file.<size>.<format>.url; primera que exista según el orden de preferencia.
    std::string mediaUrl(
        const nlohmann::json& file, std::initializer_list<std::string_view> sizes, std::string_view format
    ) {
      for (const auto size : sizes) {
        const auto sizeIt = file.find(size);
        if (sizeIt == file.end() || !sizeIt->is_object()) {
          continue;
        }
        const auto formatIt = sizeIt->find(format);
        if (formatIt != sizeIt->end() && formatIt->is_object()) {
          const std::string url = formatIt->value("url", "");
          if (url.starts_with("https://")) {
            return url;
          }
        }
      }
      return {};
    }

  } // namespace

  std::string gifsUrl(
      std::string_view key, std::string_view query, int page, std::string_view customerId
  ) {
    std::string url = std::format(
        "https://api.klipy.com/api/v1/{}/gifs/{}?page={}&per_page={}&customer_id={}", StringUtils::urlEncode(key),
        query.empty() ? "trending" : "search", page, kGifsPerPage, StringUtils::urlEncode(customerId)
    );
    if (!query.empty()) {
      url += "&q=" + StringUtils::urlEncode(query);
    }
    return url;
  }

  std::optional<GifPage> parseGifPage(std::string_view json) {
    const nlohmann::json root = nlohmann::json::parse(json, nullptr, false);
    if (!root.is_object() || !root.value("result", false)) {
      return std::nullopt;
    }
    const auto data = root.find("data");
    if (data == root.end() || !data->is_object()) {
      return std::nullopt;
    }
    const auto items = data->find("data");
    if (items == data->end() || !items->is_array()) {
      return std::nullopt;
    }
    GifPage page;
    page.hasNext = data->value("has_next", false);
    for (const auto& item : *items) {
      // Klipy intercala anuncios (type distinto de "gif") en algunas respuestas: no se muestran.
      if (!item.is_object() || item.value("type", "gif") != "gif" || !item.contains("file") || !item["file"].is_object()) {
        continue;
      }
      const auto& file = item["file"];
      Gif gif{
          .id = item.contains("id") ? item["id"].dump() : item.value("slug", ""),
          .title = item.value("title", ""),
          .url = mediaUrl(file, {"hd", "md", "sm"}, "gif"),
          .thumb = mediaUrl(file, {"sm", "xs", "md"}, "jpg"),
          .anim = mediaUrl(file, {"sm", "xs", "md"}, "gif"),
      };
      // El id va al nombre de los archivos en caché: solo dígitos y letras.
      std::erase_if(gif.id, [](char c) { return !std::isalnum(static_cast<unsigned char>(c)); });
      if (!gif.id.empty() && !gif.url.empty() && !gif.thumb.empty()) {
        page.gifs.push_back(std::move(gif));
      }
    }
    return page;
  }

} // namespace okaru::emoji
