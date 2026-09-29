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

   async function copyText(theText) {
      if(navigator.clipboard?.writeText) {
         await navigator.clipboard.writeText(theText);
         return;
      }

      const textArea = document.createElement("textarea");
      textArea.value = theText;
      textArea.style.position = "fixed";
      textArea.style.opacity = "0";
      document.body.appendChild(textArea);
      textArea.select();
      document.execCommand("copy");
      textArea.remove();
   }

   function installCodeCopyButtons() {
      document.querySelectorAll("pre > code").forEach((code) => {
         if(code.classList.contains("language-mermaid")) {
            return;
         }

         let text = code.textContent.replace(/\r\n/g, "\n");
         text = text.replace(/\n$/, "");

         if(text.length === 0) {
            return;
         }

         const pre = code.parentElement;
         if(!pre || pre.querySelector(".copy-code-button")) {
            return;
         }

         const boCommand =
            code.classList.contains("language-cmd") &&
            !text.includes("\n");

         pre.classList.add("copyable-code");

         const button = document.createElement("button");
         button.type = "button";
         button.className = "copy-code-button";
         button.textContent = "Copy";
         button.title = boCommand
            ? "Copy command to clipboard"
            : "Copy complete block to clipboard";

         button.addEventListener("click", async () => {
            try {
               await copyText(text);
               button.textContent = "Copied";
               window.setTimeout(() => {
                  button.textContent = "Copy";
               }, 1200);
            }
            catch {
               button.textContent = "Copy failed";
               window.setTimeout(() => {
                  button.textContent = "Copy";
               }, 1800);
            }
         });

         pre.appendChild(button);
      });
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
      installCodeCopyButtons();

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
