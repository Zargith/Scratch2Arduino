// Button of the title bar of the Live panels (live values, serial monitor, wiring check, virtual board):
// folds a panel to its title bar and unfolds it, so it stays within reach without taking room.
import { ActionIcon, Tooltip } from "@mantine/core";
import { IconChevronDown, IconChevronUp } from "@tabler/icons-react";

import type { MessageKey } from "@/i18n";

interface FoldButtonProps {
	folded: boolean;
	onToggle(): void;
	t(key: MessageKey): string;
}

export function FoldButton({ folded, onToggle, t }: FoldButtonProps) {
	const label = t(folded ? "panel.unfold" : "panel.fold");
	return (
		<Tooltip label={label}>
			<ActionIcon variant="subtle" color="gray" size="sm" aria-label={label} aria-expanded={!folded} onClick={onToggle}>
				{folded ? <IconChevronDown size={16} /> : <IconChevronUp size={16} />}
			</ActionIcon>
		</Tooltip>
	);
}
