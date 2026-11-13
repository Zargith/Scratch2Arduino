// Wiring check of the Live mode: the inputs that looked badly wired when Live mode started.
import { ActionIcon, Button, Card, Group, Stack, Text } from "@mantine/core";
import { IconPlugConnectedX, IconX } from "@tabler/icons-react";
import { useState } from "react";

import { FoldButton } from "@/FoldButton";
import type { MessageKey } from "@/i18n";

export interface WiringHint {
	message: string;
	blockIds: string[];
}

interface LiveWiringProps {
	hints: WiringHint[];
	t(key: MessageKey, params?: Record<string, string>): string;
	/** Lights the blocks reading the input. */
	onShowBlocks(blockIds: string[]): void;
	onClose(): void;
}

export function LiveWiring({ hints, t, onShowBlocks, onClose }: LiveWiringProps) {
	const [folded, setFolded] = useState(false);
	return (
		<Card withBorder shadow="sm" w={360} style={{ borderColor: "var(--mantine-color-yellow-6)" }}>
			<Group justify="space-between" wrap="nowrap" mb={4}>
				<Group gap={6} wrap="nowrap">
					<IconPlugConnectedX size={18} color="var(--mantine-color-yellow-7)" />
					<Text fw={700} size="sm">
						{t("live.wiring_title")}
					</Text>
				</Group>
				<Group gap={4} wrap="nowrap">
					<FoldButton folded={folded} onToggle={() => setFolded(!folded)} t={t} />
					<ActionIcon variant="subtle" color="gray" aria-label={t("live.wiring_close")} onClick={onClose}>
						<IconX size={16} />
					</ActionIcon>
				</Group>
			</Group>
			<div style={{ display: folded ? "none" : "block" }}>
			<Stack gap="xs">
				{hints.map((hint) => (
					<Stack key={hint.message} gap={2}>
						<Text size="sm">{hint.message}</Text>
						{hint.blockIds.length > 0 && (
							<Button size="compact-xs" variant="subtle" style={{ alignSelf: "flex-start" }} onClick={() => onShowBlocks(hint.blockIds)}>
								{t("live.wiring_show_blocks")}
							</Button>
						)}
					</Stack>
				))}
				<Text size="xs" c="dimmed">
					{t("live.wiring_help")}
				</Text>
			</Stack>
			</div>
		</Card>
	);
}
