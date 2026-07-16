/**
 * Include the Geode headers.
 */
#include <Geode/Geode.hpp>
#include <Geode/modify/CCScheduler.hpp>
#include <Geode/binding/GJBaseGameLayer.hpp>
#include <Geode/binding/FMODAudioEngine.hpp>
#include <Geode/loader/GameEvent.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/utils/random.hpp>

#include "PresetSetting.hpp"

/**
 * Brings cocos2d and all Geode namespaces to the current scope.
 */
using namespace geode::prelude;

/**
 * The ID used for the speed indicator label so it can be found again (or
 * lazily created) from the scheduler hook every frame.
 */
static constexpr auto SPEED_LABEL_ID = "speed-label"_spr;

/**
 * Whether the "panic"/trolling button is currently held down. Updated by the
 * keybind listener registered in $on_game(Loaded). While true, all effects are
 * suppressed and the speed snaps back to normal.
 */
static bool g_panicHeld = false;

/**
 * Hooks the global cocos2d scheduler so we can smoothly drift the game's
 * time scale toward randomized targets while gameplay is active. This
 * affects every scheduled update in the engine, which is how GD implements
 * slow-motion/fast-forward effects internally.
 */
class $modify(DrunkScheduler, CCScheduler) {
	// CCScheduler isn't a CCNode, so Geode's per-instance `m_fields` mechanism
	// can't be used here (there's only ever one scheduler anyway, so plain
	// static state works fine).
	static inline float s_currentScale = 1.f;
	static inline float s_targetScale = 1.f;
	static inline float s_timer = 0.f;
	static inline float s_shakeCooldown = 0.f;

	// Finds the speed indicator label on the UI layer, creating it if it
	// doesn't exist yet. Attaching to m_uiLayer keeps it fixed on screen.
	CCLabelBMFont* getOrCreateLabel(GJBaseGameLayer* gjbgl) {
		auto uiLayer = gjbgl->m_uiLayer;
		if (!uiLayer) return nullptr;

		if (auto existing = typeinfo_cast<CCLabelBMFont*>(uiLayer->getChildByID(SPEED_LABEL_ID))) {
			return existing;
		}

		auto label = CCLabelBMFont::create("1.00x", "bigFont.fnt");
		label->setID(SPEED_LABEL_ID);
		label->setScale(0.4f);
		label->setAnchorPoint({1.f, 1.f});
		label->setOpacity(180);

		auto winSize = CCDirector::sharedDirector()->getWinSize();
		label->setPosition({winSize.width - 8.f, winSize.height - 8.f});

		uiLayer->addChild(label, 1000);
		return label;
	}

	// Applies the current speed multiplier to the background music pitch so the
	// song speeds up / slows down along with gameplay.
	void applyMusicSpeed(float scale) {
		auto engine = FMODAudioEngine::sharedEngine();
		if (!engine || !engine->m_backgroundMusicChannel) return;
		engine->m_backgroundMusicChannel->setPitch(scale);
	}

	void update(float dt) {
		auto mod = Mod::get();
		bool enabled = mod->getSettingValue<bool>("enabled");
		auto gjbgl = GJBaseGameLayer::get();
		bool inGameplay = gjbgl != nullptr;

		// The trolling panic button overrides everything while held.
		bool active = enabled && inGameplay && !g_panicHeld;

		if (active) {
			s_timer -= dt;
			if (s_timer <= 0.f) {
				float minSpeed = static_cast<float>(mod->getSettingValue<double>("min-speed"));
				float maxSpeed = static_cast<float>(mod->getSettingValue<double>("max-speed"));
				s_targetScale = utils::random::generate<float>(minSpeed, maxSpeed);

				if (mod->getSettingValue<bool>("randomize-interval")) {
					float minInterval = static_cast<float>(mod->getSettingValue<double>("min-interval"));
					float maxInterval = static_cast<float>(mod->getSettingValue<double>("max-interval"));
					s_timer = utils::random::generate<float>(minInterval, maxInterval);
				} else {
					s_timer = static_cast<float>(mod->getSettingValue<double>("interval"));
				}
			}

			// Smoothly ease toward the target speed instead of snapping to it.
			float smoothing = static_cast<float>(mod->getSettingValue<double>("smoothing"));
			s_currentScale += (s_targetScale - s_currentScale) * std::min(dt * smoothing, 1.f);
			this->setTimeScale(s_currentScale);

			if (mod->getSettingValue<bool>("music-speed")) {
				applyMusicSpeed(s_currentScale);
			}

			// Shake the screen (using GD's built-in camera shake) when enabled.
			// Strength scales with how far the speed is from normal.
			if (mod->getSettingValue<bool>("screen-shake")) {
				s_shakeCooldown -= dt;
				if (s_shakeCooldown <= 0.f) {
					float strength = std::abs(s_currentScale - 1.f) * 8.f;
					gjbgl->shakeCamera(0.3f, strength, 0.05f);
					s_shakeCooldown = 0.25f;
				}
			}
		} else {
			// Not active (disabled, not in gameplay, or panic held): return to
			// normal speed / pitch.
			if (this->getTimeScale() != 1.f) {
				this->setTimeScale(1.f);
			}
			s_currentScale = 1.f;
			s_targetScale = 1.f;
			s_timer = 0.f;
			if (inGameplay && mod->getSettingValue<bool>("music-speed")) {
				applyMusicSpeed(1.f);
			}
		}

		// Keep the speed indicator label up to date.
		if (inGameplay) {
			if (auto label = getOrCreateLabel(gjbgl)) {
				label->setString(fmt::format("{:.2f}x", s_currentScale).c_str());
				label->setVisible(enabled);
			}
		}

		CCScheduler::update(dt);
	}
};

/**
 * Register the custom preset selector setting type before settings are parsed.
 */
$on_mod(Loaded) {
	(void)Mod::get()->registerCustomSettingType("preset-selector", &PresetSettingV3::parse);

	// Immediately reset the time scale to normal if the user disables the
	// effect mid-gameplay, rather than waiting for the next drift cycle.
	listenForSettingChanges<bool>("enabled", [](bool value) {
		if (!value) {
			CCDirector::sharedDirector()->getScheduler()->setTimeScale(1.f);
			if (auto engine = FMODAudioEngine::sharedEngine(); engine && engine->m_backgroundMusicChannel) {
				engine->m_backgroundMusicChannel->setPitch(1.f);
			}
		}
	});
}

/**
 * Listen for the trolling "panic button" keybind so effects can be suppressed
 * while it is held down.
 */
$on_game(Loaded) {
	listenForKeybindSettingPresses(
		"panic-button",
		[](Keybind const&, bool down, bool, double) {
			if (Mod::get()->getSettingValue<bool>("panic-button-enabled")) {
				g_panicHeld = down;
			} else {
				g_panicHeld = false;
			}
		}
	);
}