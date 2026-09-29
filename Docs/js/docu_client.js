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

   function installCommandCopyButtons() {
      document.querySelectorAll("pre > code.language-cmd").forEach((code) => {
         let command = code.textContent.replace(/\r\n/g, "\n");
         command = command.replace(/\n$/, "");

         if(command.length === 0 || command.includes("\n")) {
            return;
         }

         const pre = code.parentElement;
         if(!pre || pre.querySelector(".copy-command-button")) {
            return;
         }

         pre.classList.add("copy-command");

         const button = document.createElement("button");
         button.type = "button";
         button.className = "copy-command-button";
         button.textContent = "Copy";
         button.title = "Copy command to clipboard";

         button.addEventListener("click", async () => {
            try {
               await copyText(command);
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
      installCommandCopyButtons();

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
