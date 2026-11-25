// Blocks locked by the interactive tutorial (steps already passed).
import * as Blockly from "scratch-blocks";

/**
 * Locks the blocks of `ids` (and the shadow blocks in their slots, like the number of a "wait"):
 * not movable, deletable or editable. Every other block is unlocked, so a lock saved with the
 * workspace (the editor is rebuilt when the view or the language changes) never stays behind.
 * Blocks can still be added in their free slots or under them. Locking a block clears the undo history.
 */
export function applyLocks(workspace: Blockly.Workspace, ids: readonly string[]) {
	const locked = new Set(ids);
	const isLocked = (block: Blockly.Block): boolean =>
		locked.has(block.id) || (block.isShadow() && !!block.getParent() && isLocked(block.getParent()!));
	let newlyLocked = false;
	for (const block of workspace.getAllBlocks(false)) {
		const lock = isLocked(block);
		if (block.isMovable() === lock || block.isDeletable() === lock || block.isEditable() === lock) {
			newlyLocked ||= lock;
			block.setMovable(!lock);
			block.setDeletable(!lock);
			block.setEditable(!lock);
		}
	}
	// Undo replays the recorded edits whatever the locks: Ctrl+Z could remove a locked block
	// (audit #195). The edits made before the lock cannot be undone any more.
	if (newlyLocked) workspace.clearUndo();
}
