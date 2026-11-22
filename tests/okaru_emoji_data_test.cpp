// Prueba de los datos de okaru-emoji: carga de los JSON, búsqueda, tono de piel, recientes, estado y Klipy.
#include "apps/emoji/emoji_catalog.h"
#include "apps/emoji/klipy.h"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <string>

using namespace okaru::emoji;

int main() {
  const std::filesystem::path data = EMOJI_DATA_DIR;

  const Catalog emoji = loadEmoji(data / "emoji.json");
  assert(emoji.items.size() > 1800);
  assert(emoji.categories.size() == 9);
  const Catalog kaomoji = loadGrouped(data / "kaomoji.json");
  const Catalog symbols = loadGrouped(data / "symbols.json");
  assert(!kaomoji.items.empty() && !symbols.items.empty());

  // Búsqueda: todas las palabras, sin distinguir mayúsculas; vacía = filtra por categoría.
  const auto hearts = filter(emoji, "Red HEART", "");
  assert(!hearts.empty());
  assert(emoji.items[hearts.front()].text == "❤️");
  assert(filter(emoji, "", "flags").size() == 262);
  assert(filter(emoji, "zzzz-no-existe", "").empty());
  const auto flip = filter(kaomoji, "table flip", "");
  assert(!flip.empty() && kaomoji.items[flip.front()].text.starts_with("(╯°□°）╯"));
  assert(!filter(symbols, "euro", "").empty());

  // Tono: va tras el primer carácter y quita U+FE0F.
  assert(withTone("👋", 0) == "👋");
  assert(withTone("👋", 1) == "👋🏻");
  assert(withTone("👋", 5) == "👋🏿");
  assert(withTone("☝️", 3) == "☝🏽");
  assert(withTone("🏃‍♂️", 2) == "🏃🏼‍♂️");
  const auto wave = filter(emoji, "waving hand", "");
  assert(!wave.empty() && emoji.items[wave.front()].tone);
  assert(!emoji.items[filter(emoji, "grinning face", "").front()].tone);

  // Recientes: sin repetidos, lo último primero y con tope.
  State state;
  addRecent(state, "a");
  addRecent(state, "b");
  addRecent(state, "a");
  assert((state.recents == std::vector<std::string>{"a", "b"}));
  for (int i = 0; i < 100; ++i) {
    addRecent(state, std::to_string(i));
  }
  assert(state.recents.size() == kMaxRecents && state.recents.front() == "99");

  // Estado: ida y vuelta por TOML.
  const std::filesystem::path path = std::filesystem::temp_directory_path() / "okaru_emoji_state_test.toml";
  state.tone = 4;
  state.tab = Tab::Kaomoji;
  state.customerId = "abc";
  assert(saveState(state, path));
  const State loaded = loadState(path);
  assert(loaded.recents == state.recents && loaded.tone == 4 && loaded.tab == Tab::Kaomoji);
  assert(loaded.customerId == "abc");
  std::filesystem::remove(path);
  assert(loadState(path).customerId.size() == 32); // primera apertura: id nuevo

  // Klipy: URL y lectura de la respuesta (con un anuncio que se descarta).
  const std::string trending = gifsUrl("KEY", "", 1, "cid");
  assert(trending == "https://api.klipy.com/api/v1/KEY/gifs/trending?page=1&per_page=24&customer_id=cid");
  assert(gifsUrl("KEY", "hola mundo", 2, "cid").ends_with("&q=hola%20mundo"));
  const auto page = parseGifPage(R"({"result":true,"data":{"data":[
      {"id":123,"slug":"x","title":"Gato","type":"gif","file":{
        "hd":{"gif":{"url":"https://a/hd.gif"}},
        "sm":{"gif":{"url":"https://a/sm.gif"},"jpg":{"url":"https://a/sm.jpg"}}}},
      {"id":9,"type":"ad","file":{"hd":{"gif":{"url":"https://a/ad.gif"}},"sm":{"jpg":{"url":"https://a/ad.jpg"}}}},
      {"id":7,"title":"sin miniatura","file":{"hd":{"gif":{"url":"https://a/x.gif"}}}}
    ],"has_next":true}})");
  assert(page.has_value() && page->hasNext && page->gifs.size() == 1);
  assert(page->gifs[0].id == "123" && page->gifs[0].url == "https://a/hd.gif");
  assert(page->gifs[0].thumb == "https://a/sm.jpg" && page->gifs[0].anim == "https://a/sm.gif");
  assert(!parseGifPage(R"({"result":false})").has_value());
  assert(!parseGifPage("no es json").has_value());

  std::puts("okaru_emoji_data: OK");
  return 0;
}
