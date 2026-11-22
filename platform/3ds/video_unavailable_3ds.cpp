// Original movie requests are dispatched outside the world update loop;
// FMV gallery UI runs in-world using native blot/quad submission.
#include "fmv.h"
#include "content.h"
#include "gui_layout.h"
#include "game.h"
#include "screen.h"
#include "sound.h"
#include "ui.h"
#include "font.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <iterator>
#include <string>
#include <vector>

extern void CaneGlyphsNative(const std::vector<BLOTLINEVERTEX>&, GLuint);

namespace
{
    constexpr OID STARTUP_SPLASH_QUEUE_ID = static_cast<OID>(0x10000344);

    std::deque<int> movies;

    void enqueueCutsceneIndex(int index)
    {
        if (index < 0 || index >= CUTSCENE_COUNT)
            return;
        if (g_pgsCur != nullptr)
            g_pgsCur->unlockedCutscenes |= 1U << index;
        if (movies.size() < 4)
            movies.push_back(index);
    }

    const char* const s_achzFmvTitle[] =
    {
        "The Secret Police File",
        "The Tokyo Police File",
        "The Making Of Sly Cooper",
        "Commercials & Outtakes",
        "Tide of Terror",
        "Raleigh Hung out to Dry",
        "Sunset Snake Eyes",
        "Muggshot Busted",
        "Vicious Voodoo",
        "Mz Ruby Vexed",
        "Fire in the Sky",
        "The Panda King on Ice",
        "The Cold Heart of Hate",
        "Ten Seconds",
        "The Thievius Raccoonus",
        "Credits",
        "Additional Cutscene",
        "Startup Splash"
    };

    const char* PchzLocalizedFmvTitle(int index)
    {
        if (FLocalizedContent() && index >= 0 && index < 16)
        {
            if (const char* localized = ContentLocalizedText(173 + static_cast<std::size_t>(index)))
                return localized;
        }

        static constexpr const char* french[] =
        {
            "Le dossier secret de la police", "Le dossier de la police de Tokyo", "Les coulisses de Sly Cooper",
            "Publicités et scènes coupées", "La marée de la terreur", "Raleigh prend l'eau",
            "Le crépuscule du serpent", "Muggshot arrêté", "Vaudou maléfique", "Mz Ruby contrariée",
            "Le feu dans le ciel", "Le Roi Panda sur la glace", "Le cœur glacé de la haine",
            "Dix secondes", "Le Volus Ratonus", "Générique", "Cinématique supplémentaire", "Écran de démarrage"
        };
        static constexpr const char* german[] =
        {
            "Die geheime Polizeiakte", "Die Polizeiakte von Tokio", "Die Entstehung von Sly Cooper",
            "Werbespots und Pannen", "Flut des Schreckens", "Raleigh auf dem Trockenen",
            "Sonnenuntergang für Snake Eyes", "Muggshot geschnappt", "Bösartiger Voodoo", "Mz Ruby verärgert",
            "Feuer am Himmel", "Der Panda-König auf Eis", "Das kalte Herz des Hasses",
            "Zehn Sekunden", "Der Thievius Raccoonus", "Abspann", "Zusätzliche Filmsequenz", "Startbild"
        };
        static constexpr const char* spanish[] =
        {
            "El archivo secreto de la policía", "El archivo de la policía de Tokio", "Cómo se hizo Sly Cooper",
            "Anuncios y tomas falsas", "La marea del terror", "Raleigh queda seco",
            "Ocaso de Snake Eyes", "Muggshot capturado", "Vudú despiadado", "Mz Ruby contrariada",
            "Fuego en el cielo", "El Rey Panda sobre hielo", "El frío corazón del odio",
            "Diez segundos", "El Latronius Mapáchibus", "Créditos", "Escena adicional", "Pantalla de inicio"
        };
        static constexpr const char* italian[] =
        {
            "Il dossier segreto della polizia", "Il dossier della polizia di Tokyo", "La creazione di Sly Cooper",
            "Pubblicità e scene tagliate", "La marea del terrore", "Raleigh all'asciutto",
            "Il tramonto di Snake Eyes", "Muggshot catturato", "Voodoo feroce", "Mz Ruby contrariata",
            "Fuoco nel cielo", "Il Re Panda sul ghiaccio", "Il gelido cuore dell'odio",
            "Dieci secondi", "Il Thievius Raccoonus", "Titoli di coda", "Filmato aggiuntivo", "Schermata iniziale"
        };

        if (index < 0 || index >= static_cast<int>(std::size(s_achzFmvTitle)))
            return ContentUiText("(unknown)", "(inconnu)", "(unbekannt)", "(desconocido)", "(sconosciuto)");
        return ContentUiText(s_achzFmvTitle[index], french[index], german[index], spanish[index], italian[index]);
    }

    int Mod4(int value)
    {
        value %= 4;
        return value < 0 ? value + 4 : value;
    }

    CFontBrx* FmvPreviewFont()
    {
        CFontBrx* preferred = FFontLoaded(4) ? PfontFromFont(4) : nullptr;
        if (preferred != nullptr && preferred->m_pbmp != nullptr &&
            preferred->m_pbmp->glDiffuseMap != 0 &&
            preferred->FValid('_') && preferred->FValid('a'))
            return preferred;

        CFontBrx* partialMatch = nullptr;
        for (CFontBrx& font : g_afontBrx)
        {
            if (font.m_pbmp == nullptr || font.m_pbmp->glDiffuseMap == 0 ||
                !font.FValid('_') || !font.FValid('a'))
                continue;

            if (partialMatch == nullptr)
                partialMatch = &font;

            bool hasEveryPreview = true;
            for (char ch = 'a'; ch <= 'p'; ++ch)
            {
                if (!font.FValid(ch))
                {
                    hasEveryPreview = false;
                    break;
                }
            }

            if (hasEveryPreview)
                return &font;
        }

        return partialMatch;
    }

    bool EnsureFmvMenuSize(FMVMENU* pfmvmenu, CFontBrx* pfontIcons)
    {
        if (pfmvmenu == nullptr || pfmvmenu->pfont == nullptr || pfontIcons == nullptr)
            return false;

        GLYFF* placeholderGlyph = pfontIcons->PglyffFromCh('_');
        if (placeholderGlyph == nullptr)
            return false;

        constexpr float previewScale = 1.3f;
        const float previewWidth = static_cast<float>(placeholderGlyph->dx + 1)
            * pfontIcons->m_rxScale * previewScale;
        const float previewHeight = static_cast<float>(pfontIcons->m_dyUnscaled)
            * pfontIcons->m_ryScale * previewScale;
        const float menuWidth = 90.0f + previewWidth * 4.0f;
        const float menuHeight = static_cast<float>(pfmvmenu->pfont->m_dyUnscaled)
            * pfmvmenu->pfont->m_ryScale + 60.0f + previewHeight * 4.0f;

        if (std::abs(pfmvmenu->dx - menuWidth) > 0.01f ||
            std::abs(pfmvmenu->dy - menuHeight) > 0.01f)
        {
            ResizeBlot(pfmvmenu, menuWidth, menuHeight);
        }

        return true;
    }
}

bool CaneHasPendingMovie3ds() { return !movies.empty(); }

int CaneTakeMovie3ds()
{
    if (movies.empty())
        return -1;
    int id = movies.front();
    movies.pop_front();
    return id;
}

const CUTSCENEINFO* PcutsceneinfoFromId(int cutsceneId)
{
    if (cutsceneId < 0 || cutsceneId >= CUTSCENE_COUNT)
        return nullptr;

    return &g_cutsceneTable[cutsceneId];
}

const CUTSCENEINFO* PcutsceneinfoFromIdentityId(OID identityId)
{
    for (const CUTSCENEINFO& cutsceneInfo : g_cutsceneTable)
    {
        if (cutsceneInfo.identityId == identityId)
            return &cutsceneInfo;
    }

    return nullptr;
}

void QueueCutscene(OID identityId)
{
    const CUTSCENEINFO* info = PcutsceneinfoFromIdentityId(identityId);
    if (info == nullptr)
        info = PcutsceneinfoFromId(static_cast<int>(identityId));
    if (info != nullptr)
        enqueueCutsceneIndex(info->cutsceneId);
}

void QueueCutsceneFileName(const char* name)
{
    if (!name)
        return;
    std::string file(name);
    auto slash = file.find_last_of("/\\");
    if (slash != std::string::npos)
        file.erase(0, slash + 1);
    for (const CUTSCENEINFO& info : g_cutsceneTable)
    {
        if (info.videoFileName == file)
        {
            enqueueCutsceneIndex(info.cutsceneId);
            return;
        }
    }
    int id = -1;
    if (std::sscanf(file.c_str(), "Cutscene_%d", &id) == 1 && id >= 0 && id < CUTSCENE_COUNT)
        enqueueCutsceneIndex(id);
    else
        std::fprintf(stderr, "Unresolved native movie request: %s\n", name);
}

bool FCutscenePending() { return !movies.empty(); }
bool FCutscenePlaying() { return false; }
void ExecutePendingCutscenes() {}

uint32_t FFmvUnlocked(int iCutscene)
{
    if (g_pgsCur == nullptr || iCutscene < 0 || iCutscene >= CUTSCENE_COUNT)
        return 0;
    return static_cast<uint32_t>(g_pgsCur->unlockedCutscenes) & (1U << iCutscene);
}

const char* PchzFriendlyFromCid(int iCutscene)
{
    if (iCutscene >= 0 && iCutscene < CUTSCENE_COUNT)
        return PchzLocalizedFmvTitle(iCutscene);
    return iCutscene == -1
        ? ContentUiText("Nil", "Aucun", "Keine", "Ninguno", "Nessuno")
        : ContentUiText("(unknown)", "(inconnu)", "(unbekannt)", "(desconocido)", "(sconosciuto)");
}

void StartupFmvMenu(FMVMENU* pfmvmenu)
{
    g_teFmv = {};
    g_teFmv.m_rgba = glm::vec4(0.0f, 75.0f / 255.0f, 125.0f / 255.0f, 1.0f);
    g_teFmv.m_ch = '-';
    g_teFmv.m_dyExtra = 1.0f;
    g_teFmv.m_ryScaling = 0.3f;
    g_teFmv.m_rxScaling = 0.3f;
    g_teFmv.m_dxExtra = 2.0f;

    pfmvmenu->pvtfmv = &g_vtfmvmenu;
}

void PostFmvMenuLoad(FMVMENU* pfmvmenu)
{
    PostBlotLoad(pfmvmenu);
    if (FFontLoaded(2))
    {
        pfmvmenu->pte = &g_teFmv;
        g_teFmv.m_pfont = PfontFromFont(2);
    }

    if (pfmvmenu->pfont != nullptr)
    {
        pfmvmenu->pfontOwned = pfmvmenu->pfont->PfontClone(1.0f, 1.0f);
        pfmvmenu->pfont = pfmvmenu->pfontOwned.get();
    }

    // Retail stores PS2 GS colors, where 0x80 is full RGB intensity.
    pfmvmenu->rgba = glm::vec4(127.0f / 128.0f, 127.0f / 128.0f,
                               127.0f / 128.0f, 223.0f / 255.0f);

    CFontBrx* pfontIcons = FmvPreviewFont();
    if (pfmvmenu->pfont == nullptr || pfontIcons == nullptr)
        return;

    EnsureFmvMenuSize(pfmvmenu, pfontIcons);
}

void OnFmvMenuActive(FMVMENU* pfmvmenu, int fActive)
{
    const bool activate = fActive != 0;
    if (activate == (pfmvmenu->fActive != 0))
        return;

    if (!activate)
    {
        RemoveGrfusr(1);
        pfmvmenu->pvtblot->pfnHideBlot(pfmvmenu);
    }
    else
    {
        AddGrfusr(1);
        g_joy.StartJoySelection();
        pfmvmenu->pvtblot->pfnShowBlot(pfmvmenu);

        int iCutscene = 15;
        while (iCutscene > 0 && FFmvUnlocked(iCutscene) == 0)
            --iCutscene;
        pfmvmenu->iCutscene = iCutscene;
    }

    pfmvmenu->fActive = activate;
}

void UpdateFmvMenuActive(FMVMENU* pfmvmenu, JOY* pjoy)
{
    if (pjoy == nullptr)
        return;

    const JOY_BUTTON confirmButton = FJapaneseContent() ? BTN_CIRCLE : BTN_CROSS;
    const JOY_BUTTON cancelButton = FJapaneseContent() ? BTN_CROSS : BTN_TRIANGLE;
    const bool exit = pjoy->IsPressed(cancelButton) || pjoy->IsPressed(BTN_SELECT) ||
                      pjoy->IsPressed(BTN_START) ||
                      (pjoy->joys == JOYS_Searching && g_clock.t >= 5.0f);
    if (exit)
    {
        pjoy->SetHandled(cancelButton);
        pjoy->SetHandled(BTN_SELECT);
        pjoy->SetHandled(BTN_START);
        PopUiActiveBlot(&g_ui);
        return;
    }

    if (pjoy->IsPressed(confirmButton))
    {
        pjoy->SetHandled(confirmButton);
        if (FFmvUnlocked(pfmvmenu->iCutscene) == 0)
            StartSound(static_cast<SFXID>(123), nullptr, nullptr, nullptr,
                       3000.0f, 300.0f, 1.0f, 0.0f, 0.0f, nullptr, nullptr);
        else
            QueueCutscene(g_cutsceneTable[pfmvmenu->iCutscene].identityId);
        return;
    }

    const int dx = pjoy->DxSelectionJoy(g_clock.tReal);
    const int dyInput = pjoy->DySelectionJoy(g_clock.tReal);
    if (dx == 0 && dyInput == 0)
        return;

    pjoy->tLatchX = g_clock.tReal + pjoy->dtLatchX;
    pjoy->tLatchY = g_clock.tReal + pjoy->dtLatchY;

    const int old = pfmvmenu->iCutscene;
    const int column = Mod4(old);
    int dy = dyInput;
    if (dx < 0 && column == 0)
        dy = -1;
    if (dx > 0 && column == 3)
        dy = 1;

    const int row = Mod4(old / 4 + dy);
    pfmvmenu->iCutscene = row * 4 + Mod4(column + dx);

    StartSound(static_cast<SFXID>(121), nullptr, nullptr, nullptr,
               3000.0f, 300.0f, 1.0f, 0.0f, 0.0f, nullptr, nullptr);
}

void DrawFmvMenu(FMVMENU* pfmvmenu)
{
    if (pfmvmenu == nullptr || pfmvmenu->pfont == nullptr)
        return;

    CFontBrx* pfontIcons = FmvPreviewFont();
    EnsureFmvMenuSize(pfmvmenu, pfontIcons);

    glm::vec4 color = pfmvmenu->rgba;
    color.a *= pfmvmenu->uOn;

    const GuiScale guiScale = GetGuiScale();
    const float scaleX = guiScale.x;
    const float scaleY = guiScale.y;
    float x, y, dx, dy;
    GetGuiScaledBlotRect(pfmvmenu, &x, &y, &dx, &dy);

    CTextBox textBox;
    textBox.SetPos(x, y);
    textBox.SetSize(dx, dy);
    textBox.SetTextColor(&color);
    textBox.SetHorizontalJust(JH_Center);
    textBox.SetVerticalJust(JV_Top);

    if (pfmvmenu->pte != nullptr && pfmvmenu->pte->m_pfont != nullptr)
    {
        const glm::vec4 edgeColor = pfmvmenu->pte->m_rgba;
        const float edgeScaleX = pfmvmenu->pte->m_rxScaling;
        const float edgeScaleY = pfmvmenu->pte->m_ryScaling;
        pfmvmenu->pte->m_rgba.a *= pfmvmenu->uOn;
        pfmvmenu->pte->m_rxScaling *= scaleX;
        pfmvmenu->pte->m_ryScaling *= scaleY;
        pfmvmenu->pte->m_pfont->EdgeRect(pfmvmenu->pte, &textBox);
        pfmvmenu->pte->m_rxScaling = edgeScaleX;
        pfmvmenu->pte->m_ryScaling = edgeScaleY;
        pfmvmenu->pte->m_rgba = edgeColor;
    }

    const char* lockedTitle = FJapaneseContent()
        ? "未解除"
        : ContentUiText("Locked", "Verrouillé", "Gesperrt", "Bloqueado", "Bloccato");
    char* title = const_cast<char*>(FFmvUnlocked(pfmvmenu->iCutscene)
        ? PchzFriendlyFromCid(pfmvmenu->iCutscene)
        : lockedTitle);
    pfmvmenu->pfont->PushScaling(scaleX, scaleY);
    pfmvmenu->pfont->DrawPchz(title, &textBox);
    pfmvmenu->pfont->PopScaling();

    if (pfontIcons == nullptr)
        return;

    GLYFF* placeholderGlyph = pfontIcons->PglyffFromCh('_');
    if (placeholderGlyph == nullptr || pfontIcons->m_pbmp == nullptr)
        return;

    const float previewScale = 1.3f;
    const float glyphWidth = static_cast<float>(placeholderGlyph->dx + 1)
        * pfontIcons->m_rxScale * previewScale * scaleX;
    const float glyphHeight = static_cast<float>(pfontIcons->m_dyUnscaled)
        * pfontIcons->m_ryScale * previewScale * scaleY;
    const float xStart = x + 18.0f * scaleX + glyphWidth * 0.5f;
    const float yStart = y
        + static_cast<float>(pfmvmenu->pfont->m_dyUnscaled) *
            pfmvmenu->pfont->m_ryScale * scaleY
        + 12.0f * scaleY
        + glyphHeight * 0.5f;
    const float xStep = glyphWidth + 18.0f * scaleX;
    const float yStep = glyphHeight + 12.0f * scaleY;

    std::vector<BLOTLINEVERTEX> verts;
    verts.reserve(16 * 6);
    const float bmpWidth = static_cast<float>(pfontIcons->m_pbmp->bmpWidth);
    const float bmpHeight = static_cast<float>(pfontIcons->m_pbmp->bmpHeight);

    for (int row = 0; row < 4; ++row)
    {
        for (int column = 0; column < 4; ++column)
        {
            const int index = row * 4 + column;
            const char icon = FFmvUnlocked(index) ? static_cast<char>('a' + index) : '_';
            GLYFF* glyph = pfontIcons->PglyffFromCh(icon);
            if (glyph == nullptr)
                continue;

            float pulse = 1.0f;
            if (index == pfmvmenu->iCutscene)
                pulse = 1.0f + (1.0f - (std::sin(g_clock.tReal * 10.0f) * 0.5f + 0.5f)) * 0.1f;

            const float centerX = xStart + column * xStep;
            const float centerY = yStart + row * yStep;
            const float width = glyphWidth * pfmvmenu->uOn * pulse;
            const float height = glyphHeight * pfmvmenu->uOn * pulse;

            const float left = centerX - width * 0.5f;
            const float top = centerY - height * 0.5f;
            const float right = centerX + width * 0.5f;
            const float bottom = centerY + height * 0.5f;

            const float s0 = glyph->x / bmpWidth;
            const float t0 = glyph->y / bmpHeight;
            const float s1 = (glyph->x + glyph->dx) / bmpWidth;
            const float t1 = (glyph->y + pfontIcons->m_dyUnscaled) / bmpHeight;

            auto add = [&](float vx, float vy, float u, float v)
            {
                verts.push_back({ glm::vec3(vx, vy, 0.0f), glm::vec2(u, v), color });
            };
            add(left,  top,    s0, t0);
            add(right, top,    s1, t0);
            add(right, bottom, s1, t1);
            add(left,  top,    s0, t0);
            add(right, bottom, s1, t1);
            add(left,  bottom, s0, t1);
        }
    }
    if (!verts.empty())
        CaneGlyphsNative(verts, pfontIcons->m_pbmp->glDiffuseMap);

    const float lineY = y
        + static_cast<float>(pfmvmenu->pfont->m_dyUnscaled) *
            pfmvmenu->pfont->m_ryScale * scaleY - scaleY;
    DrawLineScreen(x, lineY, 0.0f, x + dx,
                   lineY, 0.0f, g_teFmv.m_rgba, false);
}

CTextEdge g_teFmv;
VTFMV g_vtfmvmenu;
FMVMENU g_fmvmenu;

CUTSCENEINFO g_cutsceneTable[] =
{
    {  0, static_cast<OID>(0x340), "Cutscene_00.m2v", "Cutscene_00.wav" },
    {  1, static_cast<OID>(0x341), "Cutscene_01.m2v", "Cutscene_01.wav" },
    {  2, static_cast<OID>(0x33C), "Cutscene_02.m2v", "Cutscene_02.wav" },
    {  3, static_cast<OID>(0x33F), "Cutscene_03.m2v", "Cutscene_03.wav" },
    {  4, static_cast<OID>(0x345), "Cutscene_04.m2v", "Cutscene_04.wav" },
    {  5, static_cast<OID>(0x346), "Cutscene_05.m2v", "Cutscene_05.wav" },
    {  6, static_cast<OID>(0x33D), "Cutscene_06.m2v", "Cutscene_06.wav" },
    {  7, static_cast<OID>(0x33E), "Cutscene_07.m2v", "Cutscene_07.wav" },
    {  8, static_cast<OID>(0x347), "Cutscene_08.m2v", "Cutscene_08.wav" },
    {  9, static_cast<OID>(0x348), "Cutscene_09.m2v", "Cutscene_09.wav" },
    { 10, static_cast<OID>(0x342), "Cutscene_10.m2v", "Cutscene_10.wav" },
    { 11, static_cast<OID>(0x343), "Cutscene_11.m2v", "Cutscene_11.wav" },
    { 12, static_cast<OID>(0x336), "Cutscene_12.m2v", "Cutscene_12.wav" },
    { 13, static_cast<OID>(0x33A), "Cutscene_13.m2v", "Cutscene_13.wav" },
    { 14, static_cast<OID>(0x33B), "Cutscene_14.m2v", "Cutscene_14.wav" },
    { 15, static_cast<OID>(0x338), "Cutscene_15.m2v", "Cutscene_15.wav" },
    { 16, static_cast<OID>(0x344), "Cutscene_16.m2v", "Cutscene_16.wav" },
    { 17, STARTUP_SPLASH_QUEUE_ID, "Cutscene_17.m2v", "Cutscene_17.wav" }
};
