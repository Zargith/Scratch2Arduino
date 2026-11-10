import { Alert, Button, Group, Loader, Modal, Stack, Text } from "@mantine/core";
import { IconBluetooth } from "@tabler/icons-react";
import { useState } from "react";

import type { BluetoothBoard } from "@/bluetooth";
import { bridge } from "@/bridge";
import type { Language } from "@/catalog";
import type { MessageKey } from "@/i18n";

interface BluetoothDialogProps {
	opened: boolean;
	language: Language;
	t(key: MessageKey, params?: Record<string, string>): string;
	/** The user chose this board: it becomes the port of the Live mode. */
	onChosen(board: BluetoothBoard): void;
	onClose(): void;
}

type Search =
	| { state: "idle" }
	| { state: "searching" }
	| { state: "found"; boards: BluetoothBoard[] }
	| { state: "pairInSettings" }
	| { state: "failed"; message: string };

/** How to reach an ESP32 over Bluetooth: search for the boards around, then choose one. */
export function BluetoothDialog({ opened, language, t, onChosen, onClose }: BluetoothDialogProps) {
	const [search, setSearch] = useState<Search>({ state: "idle" });

	const start = async () => {
		setSearch({ state: "searching" });
		try {
			const result = await bridge.bluetoothSearch(language);
			if (!result.ok) setSearch({ state: "failed", message: result.message ?? "" });
			else if (result.byAddress === false) setSearch({ state: "pairInSettings" });
			else setSearch({ state: "found", boards: result.devices ?? [] });
		} catch (error) {
			setSearch({ state: "failed", message: error instanceof Error ? error.message : String(error) });
		}
	};

	const searching = search.state === "searching";
	return (
		<Modal
			opened={opened}
			onClose={searching ? () => undefined : onClose}
			title={
				<Group gap="xs">
					<IconBluetooth size={20} />
					<Text fw={700}>{t("bluetooth.title")}</Text>
				</Group>
			}
			size="lg"
		>
			<Stack gap="sm">
				<Text size="sm" c="dimmed">
					{t("bluetooth.first_time")}
				</Text>
				<Text size="sm">{t("bluetooth.help")}</Text>
				<Group>
					<Button loading={searching} onClick={() => void start()} leftSection={<IconBluetooth size={16} />}>
						{t("bluetooth.search")}
					</Button>
				</Group>
				{searching && (
					<Group gap="xs">
						<Loader size="xs" />
						<Text size="sm" c="dimmed">
							{t("bluetooth.searching")}
						</Text>
					</Group>
				)}
				{search.state === "found" && search.boards.length === 0 && (
					<Alert color="yellow" variant="light">
						{t("bluetooth.none")}
					</Alert>
				)}
				{search.state === "found" &&
					search.boards.map((board) => (
						<Group key={board.address} justify="space-between" wrap="nowrap">
							<Stack gap={0}>
								<Text fw={600}>{board.name}</Text>
								<Text size="xs" c="dimmed">
									{board.address}
								</Text>
							</Stack>
							<Button
								variant="light"
								onClick={() => {
									onChosen(board);
									onClose();
								}}
							>
								{t("bluetooth.choose")}
							</Button>
						</Group>
					))}
				{search.state === "pairInSettings" && (
					<Alert color="blue" variant="light">
						{t("bluetooth.mac_help")}
					</Alert>
				)}
				{search.state === "failed" && (
					<Alert color="orange" variant="light">
						{search.message}
					</Alert>
				)}
			</Stack>
		</Modal>
	);
}
