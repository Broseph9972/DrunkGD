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

using namespace geode::prelude;

static constexpr auto SPEED_LABEL_ID = "speed-label"_spr;

static bool g_panicHeld = false;

class $modify(DrunkScheduler, CCScheduler) {
	static inline float s_currentScale = 1.f;
	static inline float s_targetScale = 1.f;
	static inline float s_timer = 0.f;
	static inline float s_shakeCooldown = 0.f;

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

	void applyMusicSpeed(float scale) {
		auto engine = FMODAudioEngine::sharedEngine();
		if (!engine || !engine->m_backgroundMusicChannel) return;
		engine->m_backgroundMusicChannel->setPitch(scale);
	}

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
		bool showSpeedViewer = mod->getSettingValue<bool>("speed-viewer");
		auto gjbgl = GJBaseGameLayer::get();
		bool inGameplay = gjbgl != nullptr;

		DrunkParams params = getPresetParams(mod->getSettingValue<DrunkPreset>("preset"));

		bool active = enabled && inGameplay && !g_panicHeld;

		if (active) {
			s_timer -= dt;
			if (s_timer <= 0.f) {
				float newTarget = utils::random::generate<float>(params.minSpeed, params.maxSpeed);

				newTarget = std::clamp(newTarget, s_currentScale - params.maxStep, s_currentScale + params.maxStep);
				s_targetScale = std::clamp(newTarget, params.minSpeed, params.maxSpeed);

				if (params.randomizeInterval) {
					s_timer = utils::random::generate<float>(params.minInterval, params.maxInterval);
				} else {
					s_timer = params.interval;
				}
			}

			float rate = params.transitionTime > 0.01f ? std::min(dt / params.transitionTime, 1.f) : 1.f;
			s_currentScale += (s_targetScale - s_currentScale) * rate;

			float nativeWarp = 1.f;
			if (gjbgl) {
				auto &gs = gjbgl->m_gameState;
				if (gs.m_queuedTimeWarp != 0.f) nativeWarp = gs.m_queuedTimeWarp;
				else if (gs.m_timeWarp != 0.f) nativeWarp = gs.m_timeWarp;
			}
			float finalScale = nativeWarp * s_currentScale;
			this->setTimeScale(finalScale);

			if (params.musicSpeed) {
				applyMusicSpeed(finalScale);
			}

			if (params.gravityDrift) {
				float span = params.maxSpeed - params.minSpeed;
				float t = span > 0.0001f ? (s_currentScale - params.minSpeed) / span : 0.5f;
				t = std::clamp(t, 0.f, 1.f);
				float gravity = params.gravityMin + t * (params.gravityMax - params.gravityMin);
				applyGravity(gjbgl, gravity);
			}

			if (params.screenShake) {
				s_shakeCooldown -= dt;
				if (s_shakeCooldown <= 0.f) {
					float strength = std::abs(s_currentScale - 1.f) * 8.f;
					gjbgl->shakeCamera(0.3f, strength, 0.05f);
					s_shakeCooldown = 0.25f;
				}
			}
		} else {
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

		if (inGameplay && showSpeedViewer) {
			if (auto label = getOrCreateLabel(gjbgl)) {
				label->setString(fmt::format("{:.2f}x", s_currentScale).c_str());
				label->setVisible(enabled);
			}
		} else if (inGameplay) {
			if (auto label = typeinfo_cast<CCLabelBMFont*>(gjbgl->m_uiLayer ? gjbgl->m_uiLayer->getChildByID(SPEED_LABEL_ID) : nullptr)) {
				label->setVisible(false);
			}
		}

		CCScheduler::update(dt);
	}
};

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

$on_mod(Loaded) {
	(void)Mod::get()->registerCustomSettingType("preset-selector", &PresetSettingV3::parse);

	listenForSettingChanges<bool>("enabled", [](bool value) {
		if (!value) {
			CCDirector::sharedDirector()->getScheduler()->setTimeScale(1.f);
			if (auto engine = FMODAudioEngine::sharedEngine(); engine && engine->m_backgroundMusicChannel) {
				engine->m_backgroundMusicChannel->setPitch(1.f);
			}
		}
	});
}

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