#include "apps/emoji/emoji_window.h"

#include "apps/common/okaru_app.h"
#include "core/deferred_call.h"
#include "core/files/resource_paths.h"
#include "core/input/key_modifiers.h"
#include "core/input/key_symbols.h"
#include "core/log.h"
#include "core/process/process.h"
#include "emoji_build_config.h"
#include "i18n/i18n.h"
#include "render/core/image_decoder.h"
#include "render/core/renderer.h"
#include "render/core/texture_manager.h"
#include "ui/builders.h"
#include "ui/controls/image.h"
#include "ui/controls/input.h"
#include "ui/controls/label.h"
#include "ui/controls/virtual_grid_view.h"
#include "ui/palette.h"
#include "ui/style.h"
#include "util/file_utils.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <format>

namespace okaru::emoji {

  namespace {

    using i18n::tr;

    constexpr Logger kLog("emoji");
    constexpr std::string_view kKlipyKey = OKARU_KLIPY_KEY;
    constexpr auto kSearchDelay = std::chrono::milliseconds(400);
    constexpr int kMaxGifFrames = 120;
    constexpr std::size_t kMaxGifBytes = 48U * 1024U * 1024U;

    std::filesystem::path cacheDir() {
      const char* xdg = std::getenv("XDG_CACHE_HOME");
      const char* home = std::getenv("HOME");
      const std::filesystem::path base = xdg != nullptr && xdg[0] != '\0' ? std::filesystem::path(xdg)
          : home != nullptr                                               ? std::filesystem::path(home) / ".cache"
                                                                          : std::filesystem::temp_directory_path();
      return base / "okaru" / "emoji-gifs";
    }

    std::filesystem::path thumbPath(const Gif& gif) { return cacheDir() / (gif.id + ".jpg"); }
    std::filesystem::path animPath(const Gif& gif) { return cacheDir() / (gif.id + ".gif"); }

    bool fileExists(const std::filesystem::path& path) {
      std::error_code ec;
      return std::filesystem::exists(path, ec);
    }

    std::string categoryLabel(std::string_view id) { return tr(std::format("apps.emoji.category.{}", id)); }

    void paintTile(Flex& tile, bool selected, bool hovered) {
      if (selected) {
        tile.setFill(colorSpecFromRole(ColorRole::Primary));
      } else if (hovered) {
        tile.setFill(colorSpecFromRole(ColorRole::Hover));
      } else {
        tile.clearFill();
      }
    }

  } // namespace

  // Emojis, kaomojis y símbolos: una etiqueta centrada por celda.
  class TextGridAdapter final : public VirtualGridAdapter {
  public:
    explicit TextGridAdapter(EmojiWindow& window) : m_window(window) {}

    // El catálogo cambia sin reconstruir la escena (de «Recientes» a una categoría): refilter() lo vuelve a fijar.
    void setCatalog(const Catalog* catalog) { m_catalog = catalog; }

    void set(const Catalog* catalog, const std::vector<std::size_t>* visible, int tone, float fontSize, float scale) {
      m_catalog = catalog;
      m_visible = visible;
      m_tone = tone;
      m_fontSize = fontSize;
      m_scale = scale;
    }

    [[nodiscard]] std::string textAt(std::size_t index) const {
      const Item& item = m_catalog->items[(*m_visible)[index]];
      return item.tone ? withTone(item.text, m_tone) : item.text;
    }

    [[nodiscard]] std::size_t itemCount() const override {
      return m_catalog != nullptr && m_visible != nullptr ? m_visible->size() : 0;
    }

    [[nodiscard]] std::unique_ptr<Node> createTile() override {
      Label* label = nullptr;
      auto tile = ui::column({
          .align = FlexAlign::Center,
          .justify = FlexJustify::Center,
          .padding = 0.0F,
          .configure = [this](Flex& flex) { flex.setRadius(Style::scaledRadiusMd(m_scale)); },
      });
      tile->addChild(ui::label({.out = &label, .fontSize = m_fontSize, .maxLines = 1}));
      tile->setUserData(label);
      return tile;
    }

    void bindTile(Node& tile, std::size_t index, bool selected, bool hovered) override {
      auto& flex = static_cast<Flex&>(tile);
      auto* label = static_cast<Label*>(flex.userData());
      if (index >= itemCount() || label == nullptr) {
        tile.setVisible(false);
        return;
      }
      tile.setVisible(true);
      paintTile(flex, selected, hovered);
      label->setFontSize(m_fontSize);
      label->setColor(colorSpecFromRole(selected ? ColorRole::OnPrimary : ColorRole::OnSurface));
      label->setText(textAt(index));
    }

    void onActivate(std::size_t index) override { m_window.pick(index, EmojiWindow::PickMode::Insert); }

    [[nodiscard]] std::string itemTooltip(std::size_t index) const override {
      if (index >= itemCount()) {
        return {};
      }
      return m_catalog->items[(*m_visible)[index]].name;
    }

  private:
    EmojiWindow& m_window;
    const Catalog* m_catalog = nullptr;
    const std::vector<std::size_t>* m_visible = nullptr;
    int m_tone = 0;
    float m_fontSize = 24.0F;
    float m_scale = 1.0F;
  };

  // GIF: miniatura quieta; la celda bajo el puntero se anima con su GIF pequeño.
  class GifGridAdapter final : public VirtualGridAdapter {
  public:
    explicit GifGridAdapter(EmojiWindow& window) : m_window(window) {}
    ~GifGridAdapter() override { stopAnimation(); }

    GifGridAdapter(const GifGridAdapter&) = delete;
    GifGridAdapter& operator=(const GifGridAdapter&) = delete;

    void set(const std::vector<Gif>* gifs, float scale) {
      m_gifs = gifs;
      m_scale = scale;
    }

    // Antes de reconstruir la escena: las celdas (y su Image) se destruyen.
    void reset() { stopAnimation(); }

    [[nodiscard]] std::size_t itemCount() const override { return m_gifs != nullptr ? m_gifs->size() : 0; }

    [[nodiscard]] std::unique_ptr<Node> createTile() override {
      Image* image = nullptr;
      auto tile = ui::column({
          .align = FlexAlign::Stretch,
          .padding = Style::spaceXs * m_scale,
          .clipChildren = true,
          .configure = [this](Flex& flex) { flex.setRadius(Style::scaledRadiusMd(m_scale)); },
      });
      tile->addChild(ui::image({
          .out = &image,
          .fit = ImageFit::Cover,
          .radius = Style::scaledRadiusSm(m_scale),
          .flexGrow = 1.0F,
      }));
      tile->setUserData(image);
      return tile;
    }

    void bindTile(Node& tile, std::size_t index, bool selected, bool hovered) override {
      auto& flex = static_cast<Flex&>(tile);
      auto* image = static_cast<Image*>(flex.userData());
      Renderer* renderer = m_window.renderer();
      if (index >= itemCount() || image == nullptr || renderer == nullptr) {
        tile.setVisible(false);
        return;
      }
      tile.setVisible(true);
      paintTile(flex, selected, hovered);
      const Gif& gif = (*m_gifs)[index];

      // La celda se reutiliza para otro GIF o el puntero se fue: la animación de esta celda termina.
      if (image == m_animImage && (index != m_animIndex || !hovered)) {
        stopAnimation();
      }
      if (hovered && image != m_animImage) {
        startAnimation(*renderer, *image, index, gif);
      }
      if (image != m_animImage) {
        const std::filesystem::path thumb = thumbPath(gif);
        if (fileExists(thumb)) {
          (void)image->setSourceFile(*renderer, thumb.string(), static_cast<int>(160.0F * m_scale));
        } else {
          image->clear(*renderer);
        }
      }
      // Cerca del final de lo cargado: la página siguiente.
      if (index + 6 >= itemCount()) {
        DeferredCall::callLater([window = &m_window] { window->loadMoreGifs(); });
      }
    }

    void onActivate(std::size_t index) override { m_window.pick(index, EmojiWindow::PickMode::Insert); }

    [[nodiscard]] std::string itemTooltip(std::size_t index) const override {
      return index < itemCount() ? (*m_gifs)[index].title : std::string{};
    }

  private:
    void startAnimation(Renderer& renderer, Image& image, std::size_t index, const Gif& gif) {
      stopAnimation();
      const std::filesystem::path path = animPath(gif);
      if (!fileExists(path)) {
        // Se baja al pasar el puntero; al terminar, la celda se vuelve a enlazar y la animación arranca.
        if (m_downloading != gif.id && !gif.anim.empty()) {
          m_downloading = gif.id;
          const std::string temp = path.string() + ".part";
          (void)process::runAsync(
              {"curl", "-fsSL", "--max-time", "20", "-o", temp.c_str(), gif.anim.c_str()},
              {.onExit = [this, temp, final = path.string()](const process::RunResult& result) {
                 DeferredCall::callLater([this, ok = result.exitCode == 0, temp, final] {
                   std::error_code ec;
                   if (ok) {
                     std::filesystem::rename(temp, final, ec);
                   } else {
                     std::filesystem::remove(temp, ec);
                   }
                   m_downloading.clear();
                   m_window.requestFrame();
                 });
               }}
          );
        }
        return;
      }
      const std::vector<std::uint8_t> bytes = FileUtils::readBinaryFile(path.string());
      auto decoded = decodeAnimatedGif(bytes.data(), bytes.size(), kMaxGifFrames, kMaxGifBytes);
      if (!decoded || decoded->frames.size() <= 1) {
        return;
      }
      for (const auto& frame : decoded->frames) {
        TextureHandle handle = renderer.textureManager().loadFromRgba(frame.rgba.data(), decoded->width, decoded->height);
        if (handle.id == 0) {
          stopAnimation();
          return;
        }
        m_frames.push_back({handle, std::chrono::milliseconds(std::max<std::int64_t>(20, frame.durationMs))});
      }
      m_renderer = &renderer;
      m_animImage = &image;
      m_animIndex = index;
      m_frame = 0;
      image.setExternalTexture(renderer, m_frames[0].handle);
      scheduleNextFrame();
    }

    void scheduleNextFrame() {
      m_timer.start(m_frames[m_frame].duration, [this] {
        if (m_animImage == nullptr || m_frames.empty()) {
          return;
        }
        m_frame = (m_frame + 1) % m_frames.size();
        m_animImage->setExternalTexture(*m_renderer, m_frames[m_frame].handle);
        m_window.requestFrame();
        scheduleNextFrame();
      });
    }

    void stopAnimation() {
      m_timer.stop();
      if (m_renderer != nullptr) {
        if (m_animImage != nullptr) {
          m_animImage->clear(*m_renderer); // suelta la textura externa antes de liberarla
        }
        for (auto& frame : m_frames) {
          m_renderer->textureManager().unload(frame.handle);
        }
      }
      m_frames.clear();
      m_animImage = nullptr;
      m_animIndex = static_cast<std::size_t>(-1);
    }

    struct Frame {
      TextureHandle handle;
      std::chrono::milliseconds duration;
    };

    EmojiWindow& m_window;
    const std::vector<Gif>* m_gifs = nullptr;
    float m_scale = 1.0F;
    Renderer* m_renderer = nullptr;
    Image* m_animImage = nullptr;
    std::size_t m_animIndex = static_cast<std::size_t>(-1);
    std::vector<Frame> m_frames;
    std::size_t m_frame = 0;
    std::string m_downloading;
    Timer m_timer;
  };

  EmojiWindow::EmojiWindow(apps::OkaruApp& app)
      : m_app(app), m_state(loadState()),
        m_emoji(loadEmoji(paths::appDataDir("emoji") / "emoji.json")),
        m_kaomoji(loadGrouped(paths::appDataDir("emoji") / "kaomoji.json")),
        m_symbols(loadGrouped(paths::appDataDir("emoji") / "symbols.json")),
        m_textAdapter(std::make_unique<TextGridAdapter>(*this)), m_gifAdapter(std::make_unique<GifGridAdapter>(*this)) {
    if (m_emoji.items.empty()) {
      kLog.warn("emoji.json not found under {}", paths::appDataDir("emoji").string());
    }
    if (m_state.tab == Tab::Gif && kKlipyKey.empty()) {
      m_state.tab = Tab::Emoji;
    }
    m_category = m_state.tab == Tab::Emoji && !m_state.recents.empty() ? "recent" : "";
    (void)saveState(m_state); // fija el customer_id de la primera apertura
    refilter();
    if (m_state.tab == Tab::Gif) {
      fetchGifs(false);
    }
    m_app.setBuild([this](float w, float h, float s) { return build(w, h, s); });
    m_app.setKeyHandler([this](const KeyboardEvent& event) { return onKey(event); });
    m_app.setAfterBuild([this] {
      if (m_search != nullptr) {
        m_app.focus(m_search->inputArea());
      }
    });
    m_app.setOnClose([this] { m_gifAdapter->reset(); });
  }

  EmojiWindow::~EmojiWindow() {
    if (m_grid != nullptr) {
      m_grid->setAdapter(nullptr);
    }
  }

  void EmojiWindow::requestFrame() {
    if (m_grid != nullptr) {
      m_grid->notifyDataChanged();
    }
    m_app.invalidate();
  }

  const Catalog* EmojiWindow::currentCatalog() const {
    switch (m_state.tab) {
    case Tab::Emoji:
      return m_category == "recent" && m_query.empty() ? &m_recents : &m_emoji;
    case Tab::Kaomoji:
      return &m_kaomoji;
    case Tab::Symbols:
      return &m_symbols;
    case Tab::Gif:
      break;
    }
    return nullptr;
  }

  void EmojiWindow::refilter() {
    if (m_state.tab == Tab::Emoji && m_category == "recent") {
      // Los recientes guardan el texto final (con tono): se muestran tal cual.
      m_recents.items.clear();
      for (const auto& text : m_state.recents) {
        const auto it = std::ranges::find(m_emoji.items, text, &Item::text);
        m_recents.items.push_back({.text = text, .name = it != m_emoji.items.end() ? it->name : text});
      }
    }
    const Catalog* catalog = currentCatalog();
    m_textAdapter->setCatalog(catalog);
    m_visible = catalog != nullptr
        ? filter(*catalog, m_query, m_category == "recent" ? std::string_view{} : std::string_view(m_category))
        : std::vector<std::size_t>{};
    if (m_grid != nullptr) {
      m_grid->setSelectedIndex(std::nullopt, false);
      m_grid->notifyDataChanged();
      m_grid->scrollView().setScrollOffset(0.0F);
    }
  }

  void EmojiWindow::setTab(Tab tab) {
    if (tab == m_state.tab) {
      return;
    }
    m_state.tab = tab;
    m_category = tab == Tab::Emoji && !m_state.recents.empty() && m_query.empty() ? "recent" : "";
    m_status.clear();
    (void)saveState(m_state);
    refilter();
    if (tab == Tab::Gif && m_gifs.empty()) {
      fetchGifs(false);
    }
    m_gifAdapter->reset();
    m_app.requestRebuild();
  }

  void EmojiWindow::onQueryChanged(const std::string& query) {
    m_query = query;
    if (m_state.tab == Tab::Gif) {
      m_searchTimer.start(kSearchDelay, [this] { fetchGifs(false); });
      return;
    }
    if (!m_query.empty() && m_category == "recent") {
      m_category.clear();
    }
    refilter();
    m_app.invalidate();
  }

  void EmojiWindow::setStatus(std::string text) {
    m_status = std::move(text);
    if (m_statusLabel != nullptr) {
      m_statusLabel->setText(m_status.empty() ? tr("apps.emoji.hint") : m_status);
    }
    m_app.invalidate();
  }

  void EmojiWindow::loadMoreGifs() {
    if (m_state.tab == Tab::Gif && m_gifHasNext && !m_gifLoading) {
      fetchGifs(true);
    }
  }

  void EmojiWindow::fetchGifs(bool nextPage) {
    if (kKlipyKey.empty()) {
      return;
    }
    if (m_app.config().config().shell.offlineMode) {
      setStatus(tr("apps.emoji.gif.offline"));
      return;
    }
    const std::uint64_t generation = nextPage ? m_gifGeneration : ++m_gifGeneration;
    const int page = nextPage ? m_gifPage + 1 : 1;
    if (!nextPage) {
      m_gifAdapter->reset();
      m_gifs.clear();
      m_gifHasNext = false;
      if (m_grid != nullptr) {
        m_grid->setSelectedIndex(std::nullopt, false);
        m_grid->notifyDataChanged();
        m_grid->scrollView().setScrollOffset(0.0F);
      }
    }
    m_gifLoading = true;
    setStatus(tr("apps.emoji.gif.loading"));
    const std::string url = gifsUrl(kKlipyKey, m_query, page, m_state.customerId);
    process::RunOptions options;
    options.timeout = std::chrono::seconds(20);
    options.maxOutputBytes = 4U * 1024U * 1024U;
    const bool started = process::runAsync(
        {"curl", "-fsSL", "--max-time", "15", url.c_str()},
        {.onExit = [this, generation, page](process::RunResult result) {
           DeferredCall::callLater([this, generation, page, result = std::move(result)] {
             if (generation != m_gifGeneration) {
               return; // la búsqueda cambió mientras tanto
             }
             m_gifLoading = false;
             const auto parsed = result.exitCode == 0 ? parseGifPage(result.out) : std::nullopt;
             if (!parsed.has_value()) {
               kLog.warn("klipy request failed (exit {}): {}", result.exitCode, result.err);
               setStatus(tr("apps.emoji.gif.error"));
               return;
             }
             const std::size_t from = m_gifs.size();
             m_gifs.insert(m_gifs.end(), parsed->gifs.begin(), parsed->gifs.end());
             m_gifPage = page;
             m_gifHasNext = parsed->hasNext;
             setStatus(m_gifs.empty() ? tr("apps.emoji.gif.empty") : std::string{});
             downloadThumbs(from);
             requestFrame();
           });
         }},
        options
    );
    if (!started) {
      m_gifLoading = false;
      setStatus(tr("apps.emoji.gif.error"));
    }
  }

  void EmojiWindow::downloadThumbs(std::size_t from) {
    std::error_code ec;
    std::filesystem::create_directories(cacheDir(), ec);
    // Una sola llamada a curl para toda la página: descarga en paralelo y avisa una vez.
    std::vector<std::string> args{"curl", "-fsSL", "--max-time", "20", "--parallel"};
    for (std::size_t i = from; i < m_gifs.size(); ++i) {
      if (!fileExists(thumbPath(m_gifs[i]))) {
        args.insert(args.end(), {"-o", thumbPath(m_gifs[i]).string(), m_gifs[i].thumb});
      }
    }
    if (args.size() == 5) {
      return;
    }
    const std::uint64_t generation = m_gifGeneration;
    (void)process::runAsync(args, {.onExit = [this, generation](const process::RunResult&) {
                              DeferredCall::callLater([this, generation] {
                                if (generation == m_gifGeneration) {
                                  requestFrame();
                                }
                              });
                            }});
  }

  void EmojiWindow::pick(std::size_t index, PickMode mode) {
    std::string text;
    if (m_state.tab == Tab::Gif) {
      if (index >= m_gifs.size()) {
        return;
      }
      text = m_gifs[index].url;
    } else {
      if (index >= m_visible.size()) {
        return;
      }
      text = m_textAdapter->textAt(index);
      addRecent(m_state, text);
      (void)saveState(m_state);
    }
    m_app.clipboard().setClipboardText(text);
    switch (mode) {
    case PickMode::Insert:
      setStatus(tr("apps.emoji.inserted", "text", m_state.tab == Tab::Gif ? tr("apps.emoji.tab.gif") : text));
      m_app.pasteKeepOpen();
      break;
    case PickMode::Paste:
      m_app.quitAndPaste();
      break;
    case PickMode::CopyAndClose:
      m_app.quit();
      break;
    }
  }

  bool EmojiWindow::onKey(const KeyboardEvent& event) {
    if (!event.pressed || event.preedit) {
      return false;
    }
    const bool ctrl = (event.modifiers & KeyMod::Ctrl) != 0;
    if (event.sym == XKB_KEY_Escape || (ctrl && (event.sym == XKB_KEY_q || event.sym == XKB_KEY_w))) {
      m_app.quit();
      return true;
    }
    if (ctrl && event.sym == XKB_KEY_Tab) {
      const int step = (event.modifiers & KeyMod::Shift) != 0 ? 3 : 1;
      Tab next = static_cast<Tab>((static_cast<int>(m_state.tab) + step) % 4);
      if (next == Tab::Gif && kKlipyKey.empty()) {
        next = static_cast<Tab>((static_cast<int>(next) + step) % 4);
      }
      setTab(next);
      return true;
    }
    if (event.sym == XKB_KEY_Return || event.sym == XKB_KEY_KP_Enter) {
      const std::size_t count = m_state.tab == Tab::Gif ? m_gifs.size() : m_visible.size();
      if (count == 0) {
        return true;
      }
      const std::size_t index = m_grid != nullptr ? m_grid->selectedIndex().value_or(0) : 0;
      pick(std::min(index, count - 1), ctrl ? PickMode::CopyAndClose : PickMode::Paste);
      return true;
    }
    // Del buscador a la rejilla con la flecha abajo; las flechas siguen dentro de la rejilla.
    if (event.sym == XKB_KEY_Down && m_grid != nullptr && !m_grid->selectedIndex().has_value()) {
      const std::size_t count = m_state.tab == Tab::Gif ? m_gifs.size() : m_visible.size();
      if (count > 0) {
        m_grid->setSelectedIndex(0);
        m_app.focus(m_grid->focusArea());
        return true;
      }
    }
    return false;
  }

  std::unique_ptr<Node> EmojiWindow::buildFilters(float scale) {
    auto row = ui::row({.align = FlexAlign::Center, .gap = Style::spaceSm * scale, .fillWidth = true});
    const Catalog* catalog = m_state.tab == Tab::Kaomoji ? &m_kaomoji
        : m_state.tab == Tab::Symbols                    ? &m_symbols
                                                         : &m_emoji;
    std::vector<std::string> ids;
    if (m_state.tab == Tab::Emoji && !m_state.recents.empty()) {
      ids.emplace_back("recent");
    }
    ids.emplace_back(""); // todas
    ids.insert(ids.end(), catalog->categories.begin(), catalog->categories.end());
    std::vector<std::string> labels;
    labels.reserve(ids.size());
    for (const auto& id : ids) {
      labels.push_back(categoryLabel(id.empty() ? "all" : id));
    }
    const auto selected = std::ranges::find(ids, m_category);
    row->addChild(ui::select({
        .options = std::move(labels),
        .selectedIndex = static_cast<std::size_t>(selected != ids.end() ? selected - ids.begin() : 0),
        .flexGrow = 1.0F,
        .onSelectionChanged = [this, ids](std::size_t index, std::string_view) {
          m_category = ids[index];
          refilter();
          m_app.invalidate();
        },
    }));
    if (m_state.tab == Tab::Emoji) {
      std::vector<std::string> tones;
      for (int tone = 0; tone <= 5; ++tone) {
        tones.push_back(withTone("\xE2\x9C\x8B", tone) + "  " + tr(std::format("apps.emoji.tone.{}", tone))); // ✋
      }
      row->addChild(ui::select({
          .options = std::move(tones),
          .selectedIndex = static_cast<std::size_t>(m_state.tone),
          .width = 170.0F * scale,
          .onSelectionChanged = [this](std::size_t index, std::string_view) {
            m_state.tone = static_cast<int>(index);
            (void)saveState(m_state);
            m_app.requestRebuild();
          },
      }));
    }
    return row;
  }

  std::unique_ptr<Node> EmojiWindow::build(float width, float height, float scale) {
    m_renderer = m_app.renderer();
    m_gifAdapter->reset();
    if (m_grid != nullptr) {
      m_grid->setAdapter(nullptr);
    }
    m_grid = nullptr;
    m_search = nullptr;
    m_statusLabel = nullptr;

    auto root = ui::node({.width = width, .height = height});
    root->addChild(ui::box({
        .fill = colorSpecFromRole(ColorRole::Surface),
        .border = colorSpecFromRole(ColorRole::OutlineVariant),
        .borderWidth = 1.0F,
        .radius = Style::scaledRadiusLg(scale),
        .width = width,
        .height = height,
    }));
    auto main = ui::column({
        .align = FlexAlign::Stretch,
        .gap = Style::spaceSm * scale,
        .padding = Style::spaceMd * scale,
        .width = width,
        .height = height,
    });

    main->addChild(ui::row(
        {.align = FlexAlign::Center, .gap = Style::spaceSm * scale, .fillWidth = true},
        ui::input({
            .out = &m_search,
            .value = m_query,
            .placeholder = tr(m_state.tab == Tab::Gif ? "apps.emoji.search-gif" : "apps.emoji.search"),
            .clearButtonEnabled = true,
            .flexGrow = 1.0F,
            .onChange = [this](const std::string& value) { onQueryChanged(value); },
        }),
        ui::button({
            .glyph = "x",
            .variant = ButtonVariant::Ghost,
            .tooltip = tr("apps.emoji.close"),
            .onClick = [this] { m_app.quit(); },
        })
    ));

    std::vector<ui::SegmentedOption> tabs{
        {.label = tr("apps.emoji.tab.emoji"), .glyph = "mood-smile"},
        {.label = tr("apps.emoji.tab.gif"), .glyph = "gif"},
        {.label = tr("apps.emoji.tab.kaomoji"), .glyph = "mood-happy"},
        {.label = tr("apps.emoji.tab.symbols"), .glyph = "math-symbols"},
    };
    const bool hasGif = !kKlipyKey.empty();
    if (!hasGif) {
      tabs.erase(tabs.begin() + 1);
    }
    const auto tabToSegment = [hasGif](Tab tab) {
      const int index = static_cast<int>(tab);
      return static_cast<std::size_t>(!hasGif && index > 1 ? index - 1 : index);
    };
    main->addChild(ui::segmented({
        .options = std::move(tabs),
        .selectedIndex = tabToSegment(m_state.tab),
        .scale = scale,
        .equalSegmentWidths = true,
        .onChange = [this, hasGif](std::size_t index) {
          const int tab = !hasGif && index >= 1 ? static_cast<int>(index) + 1 : static_cast<int>(index);
          setTab(static_cast<Tab>(tab));
        },
    }));

    if (m_state.tab != Tab::Gif) {
      main->addChild(buildFilters(scale));
    }

    const bool gif = m_state.tab == Tab::Gif;
    const bool wide = m_state.tab == Tab::Kaomoji;
    const float fontSize = (m_state.tab == Tab::Emoji ? 26.0F : wide ? Style::fontSizeBody : 22.0F) * scale;
    m_textAdapter->set(currentCatalog(), &m_visible, m_state.tone, fontSize, scale);
    m_gifAdapter->set(&m_gifs, scale);
    main->addChild(ui::virtualGridView({
        .out = &m_grid,
        .contentScale = scale,
        .minCellWidth = (gif ? 128.0F : wide ? 128.0F : 46.0F) * scale,
        .cellHeight = (wide ? 44.0F : 46.0F) * scale,
        .squareCells = gif,
        .columnGap = Style::spaceXs * scale,
        .rowGap = Style::spaceXs * scale,
        .overscanRows = 2,
        .adapter = gif ? static_cast<VirtualGridAdapter*>(m_gifAdapter.get()) : m_textAdapter.get(),
        .flexGrow = 1.0F,
    }));
    m_app.setPassiveNode(m_grid); // elegir en la rejilla no le quita el foco a la ventana donde se pega

    // Una sola línea: el estado (copiado, cargando, error) mientras lo haya; si no, la ayuda de teclas.
    auto footer = ui::row({.align = FlexAlign::Center, .gap = Style::spaceSm * scale, .fillWidth = true});
    footer->addChild(ui::label({
        .out = &m_statusLabel,
        .text = m_status.empty() ? tr("apps.emoji.hint") : m_status,
        .fontSize = Style::fontSizeCaption * scale,
        .color = colorSpecFromRole(ColorRole::OnSurfaceVariant),
        .maxLines = 1,
        .ellipsize = TextEllipsize::End,
        .flexGrow = 1.0F,
    }));
    if (gif) {
      // Klipy pide mostrar su atribución junto a los resultados.
      footer->addChild(ui::label({
          .text = "Powered by KLIPY",
          .fontSize = Style::fontSizeCaption * scale,
          .fontWeight = FontWeight::SemiBold,
          .color = colorSpecFromRole(ColorRole::OnSurfaceVariant),
      }));
    }
    main->addChild(std::move(footer));
    root->addChild(std::move(main));
    return root;
  }

} // namespace okaru::emoji
