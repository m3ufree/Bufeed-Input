// Bufeed Input (Input Buffer) — буферизация нажатий для GD 2.2091 (Geode 6.0.0)
// Автор: m3ufree

#include <Geode/Geode.hpp>
#include <Geode/binding/GJBaseGameLayer.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/binding/PlayerObject.hpp>
#include <Geode/utils/cocos.hpp>

using namespace geode::prelude;

namespace {
    // Одно буферизованное нажатие: игровое время и игрок (P1/P2)
    struct BufferedInput {
        double time;      // игровое время (m_gameState.m_totalTime) в момент нажатия
        bool isPlayer2;   // кому предназначалось нажатие
    };

    // Буфер нажатий (одна активная PlayLayer за раз)
    std::deque<BufferedInput> s_buffer;

    bool modEnabled() {
        return Mod::get()->getSettingValue<bool>("enabled");
    }

    // Окно буфера из настроек (в секундах)
    double bufferWindowSec() {
        return Mod::get()->getSettingValue<double>("buffer-window") / 1000.0;
    }
}

// $modify для GJBaseGameLayer — перехват кнопок
class $modify(InputBufferBGL, GJBaseGameLayer) {
    void handleButton(bool down, int button, bool isPlayer2) {
        // Сначала оригинальная обработка — обычные нажатия работают как раньше
        GJBaseGameLayer::handleButton(down, button, isPlayer2);

        // Буферизуем только нажатие кнопки прыжка (1), не при воспроизведении реплея
        if (!down || button != 1 || !modEnabled() || m_useReplay) return;

        // В одиночном режиме нажатия за P2 не буферизуем
        if (isPlayer2 && !m_gameState.m_isDualMode) return;

        auto player = isPlayer2 ? m_player2 : m_player1;
        if (!player || player->m_isDead) return;

        // Буферизуем только "наземные" режимы (куб/робот/паук) в воздухе:
        // для летящих режимов (корабль/шар/НЛО/вейв/свинговать) нажатие в воздухе и так работает
        bool flyingMode = player->m_isShip || player->m_isBall ||
                          player->m_isBird || player->m_isDart || player->m_isSwing;
        if (flyingMode || player->m_isOnGround) return;

        // Игрок в воздухе — кладём нажатие в буфер
        s_buffer.push_back({ m_gameState.m_totalTime, isPlayer2 });
    }
};

// $modify для PlayLayer — проигрывание буфера и индикатор
class $modify(InputBufferPL, PlayLayer) {
    // Поля, привязанные к жизни PlayLayer: при выходе буфер и лейбл очищаются
    struct Fields {
        cocos2d::CCLabelBMFont* bufLabel = nullptr;
        ~Fields() { s_buffer.clear(); }
    };

    // Проверка: может ли игрок прямо сейчас выполнить действие (прыжок/нажатие)
    bool canAct(PlayerObject* player) {
        if (!player || player->m_isDead) return false;
        // Летящие режимы могут действовать всегда; для куба/робота/паука нужна земля
        bool flyingMode = player->m_isShip || player->m_isBall ||
                          player->m_isBird || player->m_isDart || player->m_isSwing;
        return flyingMode || player->m_isOnGround;
    }

    // Эквивалент anyCanAct: может ли игрок, соответствующий записи буфера, действовать сейчас
    bool anyCanAct(bool isPlayer2) {
        auto player = isPlayer2 ? m_player2 : m_player1;
        return canAct(player);
    }

    // Обновление индикатора BUF:N в правом верхнем углу
    void updateBufLabel() {
        auto& label = m_fields->bufLabel;
        if (!label) {
            // Лейбл создаётся без опоры на Node IDs
            auto win = cocos2d::CCDirector::sharedDirector()->getWinSize();
            label = cocos2d::CCLabelBMFont::create("", "bigFont.fnt");
            label->setAnchorPoint({ 1.f, 1.f });
            label->setPosition({ win.width - 10.f, win.height - 10.f });
            label->setScale(0.5f);
            label->setZOrder(9999);
            this->addChild(label, 9999);
        }
        if (!modEnabled()) {
            label->setOpacity(0);
            return;
        }
        label->setString(fmt::format("BUF:{}", s_buffer.size()).c_str());
        label->setOpacity(s_buffer.empty() ? 0 : 180);
    }

    void update(float dt) {
        PlayLayer::update(dt);

        if (!modEnabled()) return;

        // Чистим просроченные (окно windowMs) и неактуальные записи (нет двойки / игрок мёртв)
        double now = m_gameState.m_totalTime;
        double window = bufferWindowSec();
        for (auto it = s_buffer.begin(); it != s_buffer.end();) {
            auto player = it->isPlayer2 ? m_player2 : m_player1;
            bool expired = (now - it->time) > window;                       // таймаут по окну
            bool stale = (it->isPlayer2 && !m_gameState.m_isDualMode);      // двойка не активна
            bool dead = player && player->m_isDead;                          // игрок мёртв
            if (expired || stale || dead) it = s_buffer.erase(it);
            else ++it;
        }

        // Проигрываем буфер: только при активном геймплее и когда игрок может действовать
        if (!s_buffer.empty() && this->isGameplayActive() && this->anyCanAct(s_buffer.front().isPlayer2)) {
            auto entry = s_buffer.front();
            s_buffer.pop_front();
            // Синтетическое нажатие проходит через обычный путь обработки ввода
            this->handleButton(true, 1, entry.isPlayer2);
        }

        updateBufLabel();
    }

    void resetLevel() {
        // Полный сброс буфера при рестарте уровня
        s_buffer.clear();
        PlayLayer::resetLevel();
        updateBufLabel();
    }
};
