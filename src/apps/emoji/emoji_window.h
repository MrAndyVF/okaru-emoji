#pragma once

// OKARU: interfaz de okaru-emoji. Buscador, pestañas (Emojis, GIF, Kaomoji, Símbolos) y una rejilla virtual por
// pestaña. Es una superficie layer-shell centrada: el clic pega en la ventana que tenía el foco y sigue abierta
// (la rejilla no toma el teclado); Enter pega y cierra; Ctrl+Enter copia y cierra.

#include "apps/emoji/emoji_catalog.h"
#include "apps/emoji/klipy.h"
#include "core/timer_manager.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class Input;
class Label;
class Node;
class Renderer;
class VirtualGridView;
struct KeyboardEvent;

namespace okaru::apps {
  class OkaruApp;
}

namespace okaru::emoji {

  class TextGridAdapter;
  class GifGridAdapter;

  class EmojiWindow {
  public:
    explicit EmojiWindow(apps::OkaruApp& app);
    ~EmojiWindow();

    EmojiWindow(const EmojiWindow&) = delete;
    EmojiWindow& operator=(const EmojiWindow&) = delete;

    enum class PickMode : std::uint8_t {
      Insert,       // clic: pega en la ventana anterior y sigue abierta
      Paste,        // Enter: cierra y pega en la ventana anterior
      CopyAndClose, // Ctrl+Enter
    };

    // Elegir el elemento `index` de la rejilla visible.
    void pick(std::size_t index, PickMode mode);
    // La rejilla de GIF llegó al final de lo cargado: pedir la página siguiente.
    void loadMoreGifs();
    void requestFrame();
    [[nodiscard]] Renderer* renderer() const noexcept { return m_renderer; }

  private:
    [[nodiscard]] std::unique_ptr<Node> build(float width, float height, float scale);
    [[nodiscard]] std::unique_ptr<Node> buildFilters(float scale);
    [[nodiscard]] bool onKey(const KeyboardEvent& event);
    [[nodiscard]] const Catalog* currentCatalog() const;
    void setTab(Tab tab);
    void refilter();
    void onQueryChanged(const std::string& query);
    void fetchGifs(bool nextPage);
    void downloadThumbs(std::size_t from);
    void setStatus(std::string text);

    apps::OkaruApp& m_app;
    State m_state;
    Catalog m_emoji;
    Catalog m_kaomoji;
    Catalog m_symbols;
    Catalog m_recents; // vista "Recientes" de la pestaña de emojis, armada desde m_state.recents
    std::string m_query;
    std::string m_category; // vacío = todas; "recent" = recientes
    std::vector<std::size_t> m_visible;

    std::vector<Gif> m_gifs;
    int m_gifPage = 0;
    bool m_gifHasNext = false;
    bool m_gifLoading = false;
    std::uint64_t m_gifGeneration = 0; // descarta respuestas de búsquedas que ya cambiaron
    std::string m_status;
    Timer m_searchTimer;

    std::unique_ptr<TextGridAdapter> m_textAdapter;
    std::unique_ptr<GifGridAdapter> m_gifAdapter;
    VirtualGridView* m_grid = nullptr;
    Input* m_search = nullptr;
    Label* m_statusLabel = nullptr;
    Renderer* m_renderer = nullptr;
  };

} // namespace okaru::emoji
