om #pragma once

#include <Geode/loader/SettingV3.hpp>
#include <Geode/loader/Mod.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>

using namespace geode::prelude;

/**
 * The three "how drunk are you" presets the user can pick from at the top of
 * the settings. Each one applies a bundle of values to the advanced settings.
 */
enum class DrunkPreset {
	BarelyNoticeable,
	Drunk,
	Wasted,
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
			case DrunkPreset::Drunk: return "drunk";
			case DrunkPreset::Wasted: return "wasted";
		}
		return "drunk";
	}
	static Result<DrunkPreset> fromJson(matjson::Value const& json) {
		GEODE_UNWRAP_INTO(auto str, json.asString());
		if (str == "barely") return Ok(DrunkPreset::BarelyNoticeable);
		if (str == "wasted") return Ok(DrunkPreset::Wasted);
		return Ok(DrunkPreset::Drunk);
	}
};

template <>
struct geode::SettingTypeForValueType<DrunkPreset> {
	using SettingType = class PresetSettingV3;
};

/**
 * Applies a preset's bundle of values to the individual advanced settings.
 * Kept free so both the setting node and $on_mod(Loaded) can reuse it.
 */
inline void applyDrunkPreset(DrunkPreset preset) {
	auto mod = Mod::get();
	switch (preset) {
		case DrunkPreset::BarelyNoticeable:
			mod->setSettingValue<double>("min-speed", 0.95);
			mod->setSettingValue<double>("max-speed", 1.05);
			mod->setSettingValue<bool>("randomize-interval", true);
			mod->setSettingValue<double>("min-interval", 3.0);
			mod->setSettingValue<double>("max-interval", 8.0);
			mod->setSettingValue<double>("smoothing", 1.5);
			mod->setSettingValue<bool>("music-speed", true);
			mod->setSettingValue<bool>("screen-shake", false);
			break;
		case DrunkPreset::Drunk:
			mod->setSettingValue<double>("min-speed", 0.80);
			mod->setSettingValue<double>("max-speed", 1.20);
			mod->setSettingValue<bool>("randomize-interval", true);
			mod->setSettingValue<double>("min-interval", 2.0);
			mod->setSettingValue<double>("max-interval", 6.0);
			mod->setSettingValue<double>("smoothing", 1.5);
			mod->setSettingValue<bool>("music-speed", true);
			mod->setSettingValue<bool>("screen-shake", false);
			break;
		case DrunkPreset::Wasted:
			mod->setSettingValue<double>("min-speed", 0.60);
			mod->setSettingValue<double>("max-speed", 1.50);
			mod->setSettingValue<bool>("randomize-interval", true);
			mod->setSettingValue<double>("min-interval", 1.0);
			mod->setSettingValue<double>("max-interval", 4.0);
			mod->setSettingValue<double>("smoothing", 2.5);
			mod->setSettingValue<bool>("music-speed", true);
			mod->setSettingValue<bool>("screen-shake", true);
			break;
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
 * The UI node: three colored buttons the user picks between.
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
			{ DrunkPreset::Drunk, "Drunk", { 240, 190, 60 } },
			{ DrunkPreset::Wasted, "Wasted", { 230, 60, 60 } },
		};

		for (auto const& entry : entries) {
			auto spr = ButtonSprite::create(entry.label, "bigFont.fnt", "GJ_button_04.png", .8f);
			spr->setScale(.5f);
			spr->m_label->setColor(entry.color);
			auto btn = CCMenuItemSpriteExtra::create(
				spr, this, menu_selector(PresetSettingNodeV3::onPreset)
			);
			btn->setTag(static_cast<int>(entry.preset));
			this->getButtonMenu()->addChild(btn);
			m_buttons.push_back({ btn, entry.preset });
		}

		this->getButtonMenu()->setContentWidth(150.f);
		this->getButtonMenu()->setLayout(RowLayout::create()->setGap(4.f));

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
			spr->setScale(selected ? .58f : .5f);
		}
	}

	void onPreset(CCObject* sender) {
		auto preset = static_cast<DrunkPreset>(sender->getTag());
		this->setValue(preset, static_cast<CCNode*>(sender));
		applyDrunkPreset(preset);
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
