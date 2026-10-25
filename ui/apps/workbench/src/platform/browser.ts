// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import type { OpenedFile, Platform } from './platform';

/** Hands a blob to the browser as a download; its name as given. */
function download(suggestedName: string, blob: Blob): string {
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = suggestedName;
  document.body.appendChild(a);
  a.click();
  a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
  return suggestedName;
}

export function createBrowserPlatform(): Platform {
  return {
    kind: 'browser',
    openProjectFile(): Promise<OpenedFile | null> {
      return new Promise((resolve) => {
        const input = document.createElement('input');
        input.type = 'file';
        input.accept = '.json,application/json';
        input.style.display = 'none';
        document.body.appendChild(input);
        const done = (file: OpenedFile | null) => {
          input.remove();
          resolve(file);
        };
        input.addEventListener('change', () => {
          const file = input.files?.[0];
          if (!file) return done(null);
          file
            .text()
            .then((text) => done({ name: file.name, text }))
            .catch(() => done(null));
        });
        input.addEventListener('cancel', () => done(null));
        input.click();
      });
    },
    async saveTextFile(suggestedName, text, mime) {
      return download(suggestedName, new Blob([text], { type: mime }));
    },
    async saveBinaryFile(suggestedName, bytes, mime) {
      return download(suggestedName, new Blob([bytes as Uint8Array<ArrayBuffer>], { type: mime }));
    },
    async writeClipboard(text) {
      await navigator.clipboard?.writeText(text);
    },
    async startSidecar() {
      return null;
    },
  };
}
