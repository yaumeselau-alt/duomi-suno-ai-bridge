#pragma once
#include <juce_core/juce_core.h>
#include <map>
#include "LocalisationCatalog.h"

namespace duomi {
enum class Language { simplifiedChinese = 1, english = 2 };
inline Language languageFromId(int id) {
  return id == 2 ? Language::english : Language::simplifiedChinese;
}
// Per-instance display translation. Never changes process-global JUCE mappings,
// network payloads, logs, file names, user prompts, or audio-thread state.
inline juce::String translate(const juce::String& source, Language language) {
  if (language != Language::english) return source;
  static const auto catalogue = [] {
    std::map<juce::String, juce::String> result;
    for (const auto& entry : translations)
      result.emplace(juce::String::fromUTF8(entry.chinese),
                     juce::String::fromUTF8(entry.english));
    return result;
  }();
  if (const auto found = catalogue.find(source); found != catalogue.end())
    return found->second;

  // Match complete application-generated messages; interpolate opaque values
  // without translating/replacing their contents (including Chinese paths).
  static const Translation patterns[] = {
    {"钢琴柱式和弦：{0} 段 · 待确认 {1} · 跳过 {2} 分析窗；切换试听后可拖出。",
     "Piano chords: {0} segments · Review {1} · Skipped {2} windows. Select to preview/drag."},
    {"MIDI 已就绪：{0} 个音符", "MIDI ready: {0} notes"},
    {"完成：{0} 个音符，{1} 秒", "Done: {0} notes, {1} seconds"},
    {"分轨已完成，共 {0} 个乐器轨道", "Separation complete: {0} instrument tracks"},
    {"音频下载失败，HTTP {0}；地址可能已过期。",
     "Audio download failed, HTTP {0}. The URL may have expired."},
    {"HTTP {0}：密钥无效或接口权限不足{1}",
     "HTTP {0}: Invalid key or insufficient API permissions{1}"},
    {"HTTP {0}：请求过于频繁，请稍后再试{1}",
     "HTTP {0}: Too many requests. Try again later{1}"},
    {"API {0}：{1}", "API {0}: {1}"},
    {"任务失败：{0}", "Task failed: {0}"},
    {"任务查询返回未知状态：{0}", "Unknown task status: {0}"},
    {"分轨已完成：{0}", "Stem ready: {0}"},
    {"已导出：{0}", "Exported: {0}"},
    {"分轨 {0}", "Stem {0}"}
  };
  for (const auto& pattern : patterns) {
    const auto format = juce::String::fromUTF8(pattern.chinese);
    juce::StringArray values;
    int cursor = 0, part = 0;
    bool matched = true;
    while (part < format.length()) {
      const int marker = format.indexOf(part, "{");
      const auto literal = marker < 0 ? format.substring(part)
                                     : format.substring(part, marker);
      if (!source.substring(cursor).startsWith(literal)) {
        matched = false; break;
      }
      cursor += literal.length();
      if (marker < 0) { part = format.length(); break; }
      const int end = format.indexOf(marker, "}");
      const int next = format.indexOf(end + 1, "{");
      const auto delimiter = next < 0 ? format.substring(end + 1)
                                     : format.substring(end + 1, next);
      const int stop = delimiter.isEmpty() ? source.length()
                                           : source.indexOf(cursor, delimiter);
      if (stop < 0) { matched = false; break; }
      values.add(source.substring(cursor, stop));
      cursor = stop;
      part = end + 1;
    }
    if (!matched || cursor != source.length()) continue;
    const auto output = juce::String::fromUTF8(pattern.english);
    juce::String result;
    for (int i = 0; i < output.length();) {
      if (output[i] == '{' && i + 2 < output.length() && output[i + 2] == '}') {
        const int index = (int)output[i + 1] - '0';
        if (juce::isPositiveAndBelow(index, values.size())) result += values[index];
        i += 3;
      } else {
        result += juce::String::charToString(output[i++]);
      }
    }
    return result;
  }
  // Remote service messages and unknown user data remain verbatim.
  return source;
}
inline juce::String translate(const char* source, Language language) {
  return translate(juce::String::fromUTF8(source), language);
}
}  // namespace duomi
