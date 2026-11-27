/** A question that waits for an answer: a text, or null when cancelled. */
export interface Question {
	resolve: (answer: string | null) => void;
}

/**
 * Holds the one question shown at a time. Showing another answers the open one with null, and
 * answering clears it, so no caller is left waiting for an answer that never comes (audit #196).
 */
export function promptSlot<Q extends Question>() {
	let shown: Q | null = null;
	return {
		/** Shows `question`, cancelling the one still open. */
		show(question: Q): void {
			const previous = shown;
			shown = question;
			previous?.resolve(null);
		},
		/** Answers the open question (null: cancel), if any. */
		answer(value: string | null): void {
			const open = shown;
			shown = null;
			open?.resolve(value);
		},
	};
}
