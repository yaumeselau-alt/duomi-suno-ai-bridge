#include "PluginEditor.h"
#include <iostream>
#include <stdexcept>
namespace {
void require(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}
juce::ComboBox* languageBox(juce::Component& editor) {
  for (auto* child : editor.getChildren())
    if (child->getName() == "Language")
      return dynamic_cast<juce::ComboBox*>(child);
  return nullptr;
}
bool hasText(juce::Component& root, const juce::String& text) {
  if (auto* label = dynamic_cast<juce::Label*>(&root))
    if (label->getText() == text) return true;
  if (auto* button = dynamic_cast<juce::TextButton*>(&root))
    if (button->getButtonText() == text) return true;
  for (auto* child : root.getChildren()) if (hasText(*child, text)) return true;
  return false;
}
void snapshot(juce::Component& editor, const char* name) {
  auto folder = juce::File::getCurrentWorkingDirectory().getChildFile("language-preview");
  require(folder.createDirectory().wasOk(), "Cannot create screenshot folder");
  auto stream = folder.getChildFile(name).createOutputStream();
  require(stream != nullptr, "Cannot create screenshot");
  stream->setPosition(0); stream->truncate();
  require(juce::PNGImageFormat().writeImageToStream(
      editor.createComponentSnapshot(editor.getLocalBounds()), *stream), "PNG failed");
}
}
int runLocalisationTests() {
  using namespace duomi;
  for (const auto& entry : translations) {
    require(translate(entry.chinese, Language::english) ==
                juce::String::fromUTF8(entry.english), "Catalogue mismatch");
    require(translate(entry.chinese, Language::simplifiedChinese) ==
                juce::String::fromUTF8(entry.chinese), "Chinese changed");
  }
  require(translate("MIDI 已就绪：1774 个音符", Language::english) ==
              "MIDI ready: 1774 notes", "Dynamic status failed");
  const auto path = juce::String::fromUTF8("C:/音乐/生成 MIDI/{0}.mid");
  require(translate(juce::String::fromUTF8("已导出：") + path, Language::english) ==
              "Exported: " + path, "Export path was translated");
  require(translate("任务失败：自定义服务错误", Language::english) ==
              juce::String::fromUTF8("Task failed: 自定义服务错误"), "Service text changed");
  require(translate("HTTP 401：密钥无效或接口权限不足 details", Language::english) ==
              "HTTP 401: Invalid key or insufficient API permissions details", "HTTP status failed");
  require(translate("钢琴柱式和弦：52 段 · 待确认 27 · 跳过 3 分析窗；切换试听后可拖出。",
                    Language::english).startsWith("Piano chords: 52 segments"), "Chord status failed");

  SessionState state, decoded;
  state.language = 2;
  auto bytes = state.encode();
  require(SessionState::decode(bytes.getData(), (int)bytes.getSize(), decoded) &&
          decoded.language == 2, "Language round-trip failed");
  auto json = juce::JSON::parse(juce::String::fromUTF8((const char*)bytes.getData(), (int)bytes.getSize()));
  json.getDynamicObject()->removeProperty("language");
  auto old = juce::JSON::toString(json);
  require(SessionState::decode(old.toRawUTF8(), old.getNumBytesAsUTF8(), decoded) &&
          decoded.language == 1, "Old project must default to Chinese");
  for (auto invalid : {juce::var(3), juce::var("English")}) {
    json.getDynamicObject()->setProperty("language", invalid);
    const auto bad = juce::JSON::toString(json);
    require(!SessionState::decode(bad.toRawUTF8(), bad.getNumBytesAsUTF8(), decoded),
            "Invalid language accepted");
  }

  DuomiSunoAIBridgeAudioProcessor p, independent;
  const auto prompt = juce::String::fromUTF8("钢琴 C 大调 / piano");
  p.setDescription(prompt);
  std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
  auto* choice = languageBox(*editor);
  require(choice != nullptr && choice->getSelectedId() == 1, "Missing language control");
  choice->setSelectedId(2, juce::sendNotificationSync);
  require(hasText(*editor, "Generate MIDI") && hasText(*editor, "Rebuild piano block chords"),
          "English controls not refreshed");
  require(!hasText(*editor, juce::String::fromUTF8("生成 MIDI")), "Stale Chinese button");
  require(hasText(*editor, "MIDI preview") && hasText(*editor, "Every 2 beats"), "Selected combo text not translated");
  require(independent.getLanguage() == Language::simplifiedChinese &&
          p.getDescription() == prompt, "Language leaked to another instance or prompt");
  snapshot(*editor, "english.png");
  choice->setSelectedId(1, juce::sendNotificationSync);
  require(hasText(*editor, juce::String::fromUTF8("生成 MIDI")), "Chinese switch failed");
  snapshot(*editor, "chinese.png");
  choice->setSelectedId(2, juce::sendNotificationSync);
  editor.reset();
  editor.reset(p.createEditor());
  require(languageBox(*editor)->getSelectedId() == 2 &&
          hasText(*editor, "Generate MIDI"), "Language lost on editor reopen");
  languageBox(*editor)->setSelectedId(1, juce::sendNotificationSync);
  require(hasText(*editor, juce::String::fromUTF8(
      "钢琴窗拖出 MIDI，波形拖出 WAV；也可打开文件目录后拖入。")), "Stale English file hint");
  languageBox(*editor)->setSelectedId(2, juce::sendNotificationSync);
  p.getStateInformation(bytes);
  independent.setStateInformation(bytes.getData(), (int)bytes.getSize());
  for (int i = 0; i < 1000 && independent.isRestoringSession(); ++i) {
    independent.refreshBackgroundState(); juce::Thread::sleep(5);
  }
  require(!independent.isRestoringSession() &&
          independent.getLanguage() == Language::english &&
          independent.getDescription() == prompt, "Host restore lost language or prompt");
  std::cout << "Localization: catalogue, dynamic messages, opaque values, legacy/invalid state, "
               "instance isolation, live switching, editor reopen and host restore passed\\n";
  return 0;
}
