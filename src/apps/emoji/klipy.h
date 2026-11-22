#pragma once

// OKARU: API de GIF de Klipy (api.klipy.com/api/v1/<clave>/gifs/{trending,search}). Solo arma la URL y lee la
// respuesta; la descarga la hace la ventana con curl.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace okaru::emoji {

  struct Gif {
    std::string id;
    std::string title;
    std::string url;   // el que se pega: GIF en calidad alta (hd), o el mejor que haya
    std::string thumb; // miniatura quieta (jpg pequeño)
    std::string anim;  // GIF pequeño para animar al pasar el puntero
  };

  struct GifPage {
    std::vector<Gif> gifs;
    bool hasNext = false;
  };

  constexpr int kGifsPerPage = 24;

  // Tendencias si `query` está vacía; si no, búsqueda. `page` empieza en 1.
  [[nodiscard]] std::string gifsUrl(
      std::string_view key, std::string_view query, int page, std::string_view customerId
  );

  [[nodiscard]] std::optional<GifPage> parseGifPage(std::string_view json);

} // namespace okaru::emoji
