/** Failed autosaves in a row before the user is told (once). */
export const autosaveWarnAfter = 3;

/**
 * Which edits the host holds in its autosaved copy. An edit count counts as saved only once the
 * host has confirmed it: a failed autosave is retried at the next tick (audit #170). Edit counts
 * only grow, so a slow autosave that ends after a save never moves back to older edits.
 */
export class AutosaveTracker {
	private held = 0;
	private inFlight: number | null = null;
	private failuresInARow = 0;

	/** Whether the project at `edits` needs a new copy. */
	needs(edits: number): boolean {
		return edits !== this.held && edits !== this.inFlight;
	}

	started(edits: number): void {
		this.inFlight = edits;
	}

	succeeded(edits: number): void {
		if (this.inFlight === edits) this.inFlight = null;
		this.held = Math.max(this.held, edits);
		this.failuresInARow = 0;
	}

	/** Records a failure; true the one time the user should be warned. */
	failed(edits: number): boolean {
		if (this.inFlight === edits) this.inFlight = null;
		if (edits <= this.held) return false; // saved meanwhile: nothing was lost
		this.failuresInARow += 1;
		return this.failuresInARow === autosaveWarnAfter;
	}

	/** A copy announced with started() did not happen and failed nothing (a cancelled save). */
	released(edits: number): void {
		if (this.inFlight === edits) this.inFlight = null;
	}

	/** The project was saved or replaced: its edits up to `edits` need no copy. */
	forget(edits: number): void {
		this.held = Math.max(this.held, edits);
	}
}
