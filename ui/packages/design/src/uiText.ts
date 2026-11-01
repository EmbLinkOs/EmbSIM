// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// The words the shared packages show (the canvas, the instruments, the
// controls) in the language the application chose. A package writes its
// English text beside a key; the application hands over its translations
// with setUiMessages, and a key it does not translate keeps the English.
// Technical identifiers (pin, net, register and unit names) are not passed
// through here.

export type UiMessages = Readonly<Record<string, string>>;

let messages: UiMessages = {};

/** The application's translations for the packages, keyed as the packages name them. */
export function setUiMessages(next: UiMessages): void {
  messages = next;
}

/** `key` in the application's language, `english` when it has none; `{name}` takes `params.name`. */
export function uiText(key: string, english: string, params?: Readonly<Record<string, string | number>>): string {
  const text = messages[key] ?? english;
  if (!params) return text;
  return text.replace(/\{(\w+)\}/g, (m, name: string) => (name in params ? String(params[name]) : m));
}
