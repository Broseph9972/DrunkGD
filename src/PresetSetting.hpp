#pragma once

#include <Geode/loader/SettingV3.hpp>
#include <Geode/loader/Mod.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>

using namespace geode::prelude;

/**
 * The "how drunk are you" presets the user can pick from at the top of the
 * settings. Each preset (except Custom) is the source of truth at runtime;
 * Custom reads the individual values from the Advanced section instead.
 */
enum class DrunkPreset {
	BarelyNoticeable,
	Annoying,
	Drunk,
	Wasted,
	Custom,
};

/**
 * JSON (de)serialization for the preset enum so it can be stored as a setting
 * value. Stored as a simple string.
 */
template <>
struct matjson::Serialize<DrunkPreset> {
	static matjson::Value toJson(DrunkPreset const& value) {
		switch (value) {
			case DrunkPreset::BarelyNoticeable: return "barely";
			case DrunkPreset::Annoying: return "annoying";
			case DrunkPreset::Drunk: return "drunk";
			case DrunkPreset::Wasted: return "wasted";
			case DrunkPreset::Custom: return "custom";
		}
		return "drunk";
	}
	static Result<DrunkPreset> fromJson(matjson::Value const& json) {
		GEODE_UNWRAP_INTO(auto str, json.asString());
		if (str == "barely") return Ok(DrunkPreset::BarelyNoticeable);
		if (str == "annoying") return Ok(DrunkPreset::Annoying);
		if (str == "wasted") return Ok(DrunkPreset::Wasted);
		if (str == "custom") return Ok(DrunkPreset::Custom);
		return Ok(DrunkPreset::Drunk);
	}
};

// Forward declaration so the specialization below can refer to it at global
// scope (declaring it inside the geode namespace would create an ambiguous
// geode::PresetSettingV3).
class PresetSettingV3;

template <>
struct geode::SettingTypeForValueType<DrunkPreset> {
	using SettingType = ::PresetSettingV3;
};

/**
 * The full bundle of parameters that drive the drift effect. The scheduler
 * hook reads one of these every frame based on the selected preset.
 */
struct DrunkParams {
	float minSpeed;
	float maxSpeed;
	bool randomizeInterval;
	float interval;      // used when randomizeInterval is false
	float minInterval;
	float maxInterval;
	float maxStep;       // largest jump per re-randomize (1.0 = no limit)
	float transitionTime;
	bool musicSpeed;
	bool screenShake;
	bool gravityDrift;

	// Builds a DrunkParams from a JSON object, falling back to the given
	// defaults for any keys that are missing or the wrong type. This makes the
	// preset definitions below easy to read and edit by hand.
	static DrunkParams fromJson(matjson::Value const& j, DrunkParams const& def = {}) {
		auto num = [&](const char* key, float fallback) -> float {
			return static_cast<float>(j.contains(key) ? j[key].asDouble().unwrapOr(fallback) : fallback);
		};
		auto flag = [&](const char* key, bool fallback) -> bool {
			return j.contains(key) ? j[key].asBool().unwrapOr(fallback) : fallback;
		};
		return {
			num("min-speed", def.minSpeed),
			num("max-speed", def.maxSpeed),
			flag("randomize-interval", def.randomizeInterval),
			num("interval", def.interval),
			num("min-interval", def.minInterval),
			num("max-interval", def.maxInterval),
			num("max-step", def.maxStep),
			num("transition-time", def.transitionTime),
			flag("music-speed", def.musicSpeed),
			flag("screen-shake", def.screenShake),
			flag("gravity-drift", def.gravityDrift),
		};
	}
};

/**
 * The built-in presets, written as plain JSON so they're easy to tweak by hand.
 * Each object maps directly onto a DrunkParams. Any key you leave out just
 * falls back to a sensible default. To change how a preset feels, edit the
 * numbers here and rebuild. (The "custom" preset is handled separately - it
 * reads live from the Advanced-section settings instead.)
 */
inline matjson::Value const& drunkPresetJson() {
	static matjson::Value presets = matjson::parse(R"({
		"barely": {
			"comment":           "wdym i didnt mess with your game dude",
			"min-speed":         0.96,
			"max-speed":         1.04,
			"randomize-interval": true,
			"min-interval":      14.0,
			"max-interval":      22.0,
			"max-step":          0.03,
			"transition-time":   4.0,
			"music-speed":       true,
			"screen-shake":      false,
			"gravity-drift":     false
		},
		"annoying": {
			"comment":           "you had a 4 loko",
			"min-speed":         0.90,
			"max-speed":         1.1,
			"randomize-interval": true,
			"min-interval":      0.2,
			"max-interval":      0.6,
			"max-step":          2.0,
			"transition-time":   0.12,
			"music-speed":       true,
			"screen-shake":      false,
			"gravity-drift":     false
		},
		"drunk": {
			"comment":           "4 beers down",
			"min-speed":         0.80,
			"max-speed":         1.20,
			"randomize-interval": true,
			"min-interval":      2.0,
			"max-interval":      6.0,
			"max-step":          1.0,
			"transition-time":   0.7,
			"music-speed":       true,
			"screen-shake":      false,
			"gravity-drift":     false
		},
		"wasted": {
			"comment":           "on your 2nd six pack",
			"min-speed":         0.2,
			"max-speed":         2.50,
			"randomize-interval": true,
			"min-interval":      1.0,
			"max-interval":      3.0,
			"max-step":          3.0,
			"transition-time":   0.4,
			"music-speed":       true,
			"screen-shake":      true,
			"gravity-drift":     true
		}
	})").unwrapOr(matjson::Value::object());
	return presets;
}

/**
 * Returns the parameters for a given preset. For Custom, the values are read
 * live from the Advanced-section settings; every other preset uses the values
 * from the JSON above and ignores the Advanced section entirely.
 */
inline DrunkParams getPresetParams(DrunkPreset preset) {
	if (preset == DrunkPreset::Custom) {
		auto mod = Mod::get();
		return {
			static_cast<float>(mod->getSettingValue<double>("min-speed")),
			static_cast<float>(mod->getSettingValue<double>("max-speed")),
			mod->getSettingValue<bool>("randomize-interval"),
			static_cast<float>(mod->getSettingValue<double>("interval")),
			static_cast<float>(mod->getSettingValue<double>("min-interval")),
			static_cast<float>(mod->getSettingValue<double>("max-interval")),
			static_cast<float>(mod->getSettingValue<double>("max-step")),
			static_cast<float>(mod->getSettingValue<double>("transition-time")),
			mod->getSettingValue<bool>("music-speed"),
			mod->getSettingValue<bool>("screen-shake"),
			mod->getSettingValue<bool>("gravity-drift"),
		};
	}

	// Sensible fallback used if a key is missing from the JSON.
	static const DrunkParams defaults = { 0.80f, 1.20f, true, 4.f, 2.f, 6.f, 1.f, 0.7f, true, false, false };

	std::string key = matjson::Serialize<DrunkPreset>::toJson(preset).asString().unwrapOr("drunk");
	auto const& presets = drunkPresetJson();
	if (presets.contains(key)) {
		return DrunkParams::fromJson(presets[key], defaults);
	}
	return defaults;
}

/**
 * The setting definition. Just stores the currently selected preset.
 */
class PresetSettingV3 : public SettingBaseValueV3<DrunkPreset> {
public:
	static Result<std::shared_ptr<SettingV3>> parse(
		std::string const& key, std::string const& modID, matjson::Value const& json
	) {
		auto res = std::make_shared<PresetSettingV3>();
		auto root = checkJson(json, "PresetSettingV3");
		res->parseBaseProperties(key, modID, root);
		root.checkUnknownKeys();
		return root.ok(std::static_pointer_cast<SettingV3>(res));
	}

	SettingNodeV3* createNode(float width) override;
};

/**
 * The UI node: four colored buttons the user picks between.
 */
class PresetSettingNodeV3 : public SettingValueNodeV3<PresetSettingV3> {
protected:
	std::vector<std::pair<CCMenuItemSpriteExtra*, DrunkPreset>> m_buttons;

	bool init(std::shared_ptr<PresetSettingV3> setting, float width) {
		if (!SettingValueNodeV3::init(setting, width))
			return false;

		struct Entry {
			DrunkPreset preset;
			const char* label;
			ccColor3B color;
		};
		const Entry entries[] = {
			{ DrunkPreset::BarelyNoticeable, "Barely", { 90, 220, 90 } },
			{ DrunkPreset::Annoying, "Annoying", { 90, 190, 235 } },
			{ DrunkPreset::Drunk, "Drunk", { 240, 190, 60 } },
			{ DrunkPreset::Wasted, "Wasted", { 230, 60, 60 } },
			{ DrunkPreset::Custom, "Custom", { 210, 130, 235 } },
		};

		for (auto const& entry : entries) {
			auto spr = ButtonSprite::create(entry.label, "bigFont.fnt", "GJ_button_04.png", .8f);
			spr->setScale(.38f);
			spr->m_label->setColor(entry.color);
			auto btn = CCMenuItemSpriteExtra::create(
				spr, this, menu_selector(PresetSettingNodeV3::onPreset)
			);
			btn->setTag(static_cast<int>(entry.preset));
			this->getButtonMenu()->addChild(btn);
			m_buttons.push_back({ btn, entry.preset });
		}

		this->getButtonMenu()->setContentWidth(240.f);
		this->getButtonMenu()->setLayout(RowLayout::create()->setGap(2.f));

		this->updateState(nullptr);
		return true;
	}

	void updateState(CCNode* invoker) override {
		SettingValueNodeV3::updateState(invoker);

		auto shouldEnable = this->getSetting()->shouldEnable();
		auto current = this->getValue();

		for (auto& [btn, preset] : m_buttons) {
			bool selected = (preset == current);
			btn->setEnabled(shouldEnable);
			auto spr = static_cast<ButtonSprite*>(btn->getNormalImage());
			spr->setCascadeColorEnabled(true);
			spr->setCascadeOpacityEnabled(true);
			// Selected button is bright and slightly bigger; others dim.
			btn->setOpacity(shouldEnable ? (selected ? 255 : 130) : 90);
			spr->setOpacity(shouldEnable ? (selected ? 255 : 130) : 90);
			spr->setScale(selected ? .46f : .38f);
		}
	}

	void onPreset(CCObject* sender) {
		auto preset = static_cast<DrunkPreset>(sender->getTag());
		this->setValue(preset, static_cast<CCNode*>(sender));
	}

public:
	static PresetSettingNodeV3* create(std::shared_ptr<PresetSettingV3> setting, float width) {
		auto ret = new PresetSettingNodeV3();
		if (ret->init(setting, width)) {
			ret->autorelease();
			return ret;
		}
		delete ret;
		return nullptr;
	}
};

inline SettingNodeV3* PresetSettingV3::createNode(float width) {
	return PresetSettingNodeV3::create(
		std::static_pointer_cast<PresetSettingV3>(shared_from_this()),
		width
	);
}
