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
};

/**
 * Returns the parameters for a given preset. For Custom, the values are read
 * live from the Advanced-section settings; every other preset uses fixed,
 * hand-tuned values and ignores the Advanced section entirely.
 */
inline DrunkParams getPresetParams(DrunkPreset preset) {
	switch (preset) {
		case DrunkPreset::BarelyNoticeable:
			// Tiny, slow, occasional drift.
			return { 0.96f, 1.04f, true, 0.f, 14.f, 22.f, 0.03f, 4.0f, true, false, false };
		case DrunkPreset::Annoying:
			// Small range but changes constantly and snaps quickly.
			return { 0.96f, 1.04f, true, 0.f, 0.2f, 0.6f, 1.0f, 0.12f, true, false, false };
		case DrunkPreset::Drunk:
			// The classic experience.
			return { 0.80f, 1.20f, true, 0.f, 2.0f, 6.0f, 1.0f, 0.7f, true, false, false };
		case DrunkPreset::Wasted:
			// Wild swings, shaking, and warped gravity.
			return { 0.75f, 1.50f, true, 0.f, 1.0f, 6.0f, 1.0f, 0.4f, true, true, true };
		case DrunkPreset::Custom:
		default: {
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
	}
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
