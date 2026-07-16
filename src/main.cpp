/**
 * Include the Geode headers.
 */
#include <Geode/Geode.hpp>
#include <Geode/modify/CCScheduler.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/binding/GJBaseGameLayer.hpp>
#include <Geode/utils/random.hpp>

/**
 * Brings cocos2d and all Geode namespaces to the current scope.
 */
using namespace geode::prelude;

/**
 * The ID used for the speed indicator label so it can be found again from
 * the scheduler hook every frame.
 */
static constexpr auto SPEED_LABEL_ID = "speed-label"_spr;

/**
 * Adds a small label to the top-right corner of gameplay layers so the
 * current speed multiplier can be seen while testing.
 */
class $modify(DrunkGameLayer, GJBaseGameLayer) {
	bool init() {
		if (!GJBaseGameLayer::init()) return false;

		auto label = CCLabelBMFont::create("1.00x", "bigFont.fnt");
		label->setID(SPEED_LABEL_ID);
		label->setScale(0.4f);
		label->setAnchorPoint({1.f, 1.f});
		label->setOpacity(180);

		auto winSize = CCDirector::sharedDirector()->getWinSize();
		label->setPosition({winSize.width - 8.f, winSize.height - 8.f});
		label->setZOrder(1000);

		this->addChild(label, 1000);

		return true;
	}
};

/**
 * Hooks the global cocos2d scheduler so we can smoothly drift the game's
 * time scale toward randomized targets while gameplay is active. This
 * affects every scheduled update in the engine, which is how GD implements
 * slow-motion/fast-forward effects internally i guess man.
 */
class $modify(DrunkScheduler, CCScheduler) {
	// CCScheduler isn't a CCNode, so Geode's per-instance `m_fields` mechanism
	// can't be used here (there's only ever one scheduler anyway, so plain
	// static state works fine).
	static inline float s_currentScale = 1.f;
	static inline float s_targetScale = 1.f;
	static inline float s_timer = 0.f;

	void update(float dt) {
		auto mod = Mod::get();
		bool enabled = mod->getSettingValue<bool>("enabled");
		auto gjbgl = GJBaseGameLayer::get();
		bool inGameplay = gjbgl != nullptr;

		if (enabled && inGameplay) {
			s_timer -= dt;
			if (s_timer <= 0.f) {
				float intensity = static_cast<float>(mod->getSettingValue<double>("intensity"));
				s_targetScale = 1.f + utils::random::generate<float>(-intensity, intensity);

				float minInterval = static_cast<float>(mod->getSettingValue<double>("min-interval"));
				float maxInterval = static_cast<float>(mod->getSettingValue<double>("max-interval"));
				s_timer = utils::random::generate<float>(minInterval, maxInterval);
			}

			// Smoothly ease toward the target speed instead of snapping to it.
			s_currentScale += (s_targetScale - s_currentScale) * std::min(dt * 1.5f, 1.f);
			this->setTimeScale(s_currentScale);
		} else if (this->getTimeScale() != 1.f) {
			s_currentScale = 1.f;
			s_targetScale = 1.f;
			this->setTimeScale(1.f);
		}

		if (inGameplay) {
			if (auto label = typeinfo_cast<CCLabelBMFont*>(gjbgl->getChildByID(SPEED_LABEL_ID))) {
				label->setString(fmt::format("{:.2f}x", s_currentScale).c_str());
				label->setVisible(enabled);
			}
		}

		CCScheduler::update(dt);
	}
};

/**
 * Immediately reset the time scale to normal if the user disables the
 * effect mid-gameplay, rather than waiting for the next drift cycle.
 */
$on_mod(Loaded) {
	listenForSettingChanges<bool>("enabled", [](bool value) {
		if (!value) {
			CCDirector::sharedDirector()->getScheduler()->setTimeScale(1.f);
		}
	});
}