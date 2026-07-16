/**
 * Include the Geode headers.
 */
#include <Geode/Geode.hpp>
#include <Geode/modify/CCScheduler.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/binding/GJBaseGameLayer.hpp>
#include <Geode/binding/FMODAudioEngine.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/loader/GameEvent.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/ui/GeodeUI.hpp>
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

	// Sets the players' gravity to the given multiplier. Uses the sign of the
	// current gravity mod so flipped-gravity sections stay flipped. Pass 1.0 to
	// reset to normal gravity.
	void applyGravity(GJBaseGameLayer* gjbgl, float factor) {
		for (auto player : {gjbgl->m_player1, gjbgl->m_player2}) {
			if (!player) continue;
			float sign = player->m_gravityMod >= 0.f ? 1.f : -1.f;
			player->m_gravityMod = sign * factor;
		}
	}

	void update(float dt) {
		auto mod = Mod::get();
		bool enabled = mod->getSettingValue<bool>("enabled");
		auto gjbgl = GJBaseGameLayer::get();
		bool inGameplay = gjbgl != nullptr;

		// The selected preset is the source of truth for behaviour. Only the
		// "Custom" preset reads the individual Advanced-section settings.
		DrunkParams params = getPresetParams(mod->getSettingValue<DrunkPreset>("preset"));

		// The trolling panic button overrides everything while held.
		bool active = enabled && inGameplay && !g_panicHeld;

		if (active) {
			s_timer -= dt;
			if (s_timer <= 0.f) {
				float newTarget = utils::random::generate<float>(params.minSpeed, params.maxSpeed);

				// Limit how far the target can jump from the current speed in a
				// single step, so the drift can be kept gentle and gradual.
				newTarget = std::clamp(newTarget, s_currentScale - params.maxStep, s_currentScale + params.maxStep);
				s_targetScale = std::clamp(newTarget, params.minSpeed, params.maxSpeed);

				if (params.randomizeInterval) {
					s_timer = utils::random::generate<float>(params.minInterval, params.maxInterval);
				} else {
					s_timer = params.interval;
				}
			}

			// Smoothly ease toward the target speed instead of snapping to it.
			// "transitionTime" is roughly how many seconds a full change takes,
			// so larger values make the drift slower and less obvious.
			float rate = params.transitionTime > 0.01f ? std::min(dt / params.transitionTime, 1.f) : 1.f;
			s_currentScale += (s_targetScale - s_currentScale) * rate;
			this->setTimeScale(s_currentScale);

			if (params.musicSpeed) {
				applyMusicSpeed(s_currentScale);
			}

			if (params.gravityDrift) {
				// Map the current speed (within its min/max range) onto the
				// configured gravity range, so faster = one end, slower = other.
				float span = params.maxSpeed - params.minSpeed;
				float t = span > 0.0001f ? (s_currentScale - params.minSpeed) / span : 0.5f;
				t = std::clamp(t, 0.f, 1.f);
				float gravity = params.gravityMin + t * (params.gravityMax - params.gravityMin);
				applyGravity(gjbgl, gravity);
			}

			// Shake the screen (using GD's built-in camera shake) when enabled.
			// Strength scales with how far the speed is from normal.
			if (params.screenShake) {
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
			if (inGameplay && params.musicSpeed) {
				applyMusicSpeed(1.f);
			}
			if (inGameplay && params.gravityDrift) {
				applyGravity(gjbgl, 1.f);
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
 * Optionally add a DrunkGD button to the pause menu that opens the mod's
 * settings popup (the exact same menu as in the mod list).
 */
class $modify(DrunkPauseLayer, PauseLayer) {
	void customSetup() {
		PauseLayer::customSetup();

		if (!Mod::get()->getSettingValue<bool>("pause-menu-button")) return;

		auto spr = ButtonSprite::create("Drunk", "bigFont.fnt", "GJ_button_04.png", .8f);
		spr->setScale(0.6f);
		auto btn = CCMenuItemSpriteExtra::create(
			spr, this, menu_selector(DrunkPauseLayer::onDrunkSettings)
		);
		btn->setID("drunk-settings-button"_spr);

		auto menu = CCMenu::create();
		menu->setID("drunk-settings-menu"_spr);
		menu->addChild(btn);
		auto winSize = CCDirector::sharedDirector()->getWinSize();
		menu->setPosition({winSize.width / 2.f, 20.f});
		this->addChild(menu, 100);
	}

	void onDrunkSettings(CCObject*) {
		openSettingsPopup(Mod::get());
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