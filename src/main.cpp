/**
 * Include the Geode headers.
 */
#include <Geode/Geode.hpp>
#include <Geode/modify/CCScheduler.hpp>
#include <Geode/binding/GJBaseGameLayer.hpp>
#include <Geode/utils/random.hpp>

/**
 * Brings cocos2d and all Geode namespaces to the current scope.
 */
using namespace geode::prelude;

/**
 * Hooks the global cocos2d scheduler so we can smoothly drift the game's
 * time scale toward randomized targets while gameplay is active. This
 * affects every scheduled update in the engine, which is how GD implements
 * slow-motion/fast-forward effects internally.
 */
class $modify(DrunkScheduler, CCScheduler) {
	struct Fields {
		float m_currentScale = 1.f;
		float m_targetScale = 1.f;
		float m_timer = 0.f;
	};

	void update(float dt) {
		auto mod = Mod::get();
		bool enabled = mod->getSettingValue<bool>("enabled");
		bool inGameplay = GJBaseGameLayer::get() != nullptr;

		if (enabled && inGameplay) {
			m_fields->m_timer -= dt;
			if (m_fields->m_timer <= 0.f) {
				float intensity = static_cast<float>(mod->getSettingValue<double>("intensity"));
				m_fields->m_targetScale = 1.f + utils::random::generate<float>(-intensity, intensity);

				float minInterval = static_cast<float>(mod->getSettingValue<double>("min-interval"));
				float maxInterval = static_cast<float>(mod->getSettingValue<double>("max-interval"));
				m_fields->m_timer = utils::random::generate<float>(minInterval, maxInterval);
			}

			// Smoothly ease toward the target speed instead of snapping to it.
			m_fields->m_currentScale += (m_fields->m_targetScale - m_fields->m_currentScale) * std::min(dt * 1.5f, 1.f);
			this->setTimeScale(m_fields->m_currentScale);
		} else if (this->getTimeScale() != 1.f) {
			m_fields->m_currentScale = 1.f;
			m_fields->m_targetScale = 1.f;
			this->setTimeScale(1.f);
		}

		CCScheduler::update(dt);
	}
};

/**
 * Immediately reset the time scale to normal if the user disables the
 * effect mid-gameplay, rather than waiting for the next drift cycle.
 */
$on_mod(Loaded) {
	listenForSettingChanges("enabled", [](bool value) {
		if (!value) {
			CCDirector::sharedDirector()->getScheduler()->setTimeScale(1.f);
		}
	});
}