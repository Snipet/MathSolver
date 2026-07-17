// Tiny cross-component channel for the console UI: the command-reference
// panel (which may live outside the Notebook component tree, in the app
// sidebar) asks the prompt to take some text.

class ConsoleUiState {
  /** Pending insert request; the Notebook consumes and clears it. */
  insertRequest = $state<{ text: string; nonce: number } | null>(null);
  #nonce = 0;

  insert(text: string) {
    this.insertRequest = { text, nonce: ++this.#nonce };
  }

  consume(): string | null {
    const req = this.insertRequest;
    this.insertRequest = null;
    return req?.text ?? null;
  }
}

export const consoleUi = new ConsoleUiState();
