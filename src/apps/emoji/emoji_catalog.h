#pragma once

// OKARU: datos de okaru-emoji sin interfaz: emojis, kaomojis y símbolos (JSON de apps/emoji), búsqueda, tono de
// piel y el estado que se recuerda entre aperturas (recientes, tono, pestaña).

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace okaru::emoji {

  struct Item {
    std::string text;
    std::string name;     // nombre en inglés (emojis); vacío en kaomojis y símbolos
    std::string search;   // nombre y palabras clave en minúsculas, separados por espacios
    std::string category; // id de categoría (traducido con apps.emoji.category.<id>)
    bool tone = false;    // admite tono de piel (Emoji_Modifier_Base)
  };

  struct Catalog {
    std::vector<Item> items;
    std::vector<std::string> categories; // en el orden en que aparecen
  };

  // emoji.json: [{"emoji", "name", "keywords": [...], "category", "tone"?}]
  [[nodiscard]] Catalog loadEmoji(const std::filesystem::path& path);
  // kaomoji.json y symbols.json: [{"category", "items": [{"text", "keywords"}]}]
  [[nodiscard]] Catalog loadGrouped(const std::filesystem::path& path);

  // Índices que contienen todas las palabras de `query` (sin distinguir mayúsculas). Con `query` vacía filtra por
  // `category` (vacía = todas).
  [[nodiscard]] std::vector<std::size_t> filter(const Catalog& catalog, std::string_view query, std::string_view category);

  // Aplica el tono 1..5 (🏻..🏿) tras el primer carácter; 0 lo deja igual. Quita el selector de variación U+FE0F
  // que pudiera seguirle, porque el modificador ya fija la presentación en color.
  [[nodiscard]] std::string withTone(std::string_view emoji, int tone);

  enum class Tab : int { Emoji = 0, Gif = 1, Kaomoji = 2, Symbols = 3 };

  struct State {
    std::vector<std::string> recents; // lo último elegido primero
    int tone = 0;
    Tab tab = Tab::Emoji;
    std::string customerId; // id anónimo de esta instalación que pide Klipy
  };

  constexpr std::size_t kMaxRecents = 40;

  void addRecent(State& state, std::string text);
  [[nodiscard]] std::filesystem::path statePath();
  [[nodiscard]] State loadState(const std::filesystem::path& path = statePath());
  bool saveState(const State& state, const std::filesystem::path& path = statePath());

} // namespace okaru::emoji
