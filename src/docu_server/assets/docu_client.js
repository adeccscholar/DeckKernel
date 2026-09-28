// SPDX-FileCopyrightText: 2021 - 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0

(() => {
   "use strict";

   async function waitForMathJax() {
      if(globalThis.MathJax?.startup?.promise) {
         try {
            await globalThis.MathJax.startup.promise;
         }
         catch {
         }
      }
   }

   async function waitForFonts() {
      if(document.fonts?.ready) {
         try {
            await document.fonts.ready;
         }
         catch {
         }
      }
   }

   async function waitForMermaid() {
      for(let iAttempt = 0; iAttempt < 100; ++iAttempt) {
         const pendingSource =
            document.querySelector("pre code.language-mermaid");

         const pendingDiagram =
            [...document.querySelectorAll(".mermaid")]
               .some((element) => !element.querySelector("svg"));

         if(!pendingSource && !pendingDiagram) {
            return;
         }

         await new Promise((resolve) => {
            window.setTimeout(resolve, 50);
         });
      }
   }

   async function printDocument() {
      const button = document.getElementById("print-document");

      if(button) {
         button.disabled = true;
      }

      try {
         await Promise.all([
            waitForFonts(),
            waitForMathJax(),
            waitForMermaid()
         ]);

         await new Promise((resolve) => {
            window.requestAnimationFrame(() => {
               window.requestAnimationFrame(resolve);
            });
         });

         window.print();
      }
      finally {
         if(button) {
            button.disabled = false;
         }
      }
   }

   window.addEventListener("DOMContentLoaded", () => {
      const button = document.getElementById("print-document");

      if(button) {
         button.addEventListener("click", () => {
            void printDocument();
         });
      }
   });

   globalThis.DeckKernelDocumentation = Object.freeze({
      print: printDocument
   });
})();
