import { Alert, Button, Divider, Group, Loader, Modal, Select, Stack, Text, Title } from "@mantine/core";
import { IconBluetooth } from "@tabler/icons-react";
import { useMemo, useState } from "react";

import { type BluetoothBoard, hasBluetoothModule } from "@/bluetooth";
import { bridge, type BluetoothModule } from "@/bridge";
import { findBoard, type Language } from "@/catalog";
import type { MessageKey } from "@/i18n";

interface BluetoothDialogProps {
	opened: boolean;
	language: Language;
	boardId: string;
	/** The board's USB port (the module's pins are saved over the cable), null when none. */
	usbPort: string | null;
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

const SPEEDS = ["9600", "19200", "38400", "57600", "115200"];
/** Pins 0 and 1 carry the USB link. */
const USB_PINS = ["0", "1"];

/**
 * How to reach a board over Bluetooth: an ESP32 has it built in; an Arduino board takes an
 * HC-05 / HC-06 module, whose pins are first saved on the board over USB. Then search for the
 * boards around and choose one.
 */
export function BluetoothDialog({ opened, language, boardId, usbPort, t, onChosen, onClose }: BluetoothDialogProps) {
	const [search, setSearch] = useState<Search>({ state: "idle" });
	const [module, setModule] = useState<BluetoothModule>({ rx: "10", tx: "11", baud: 9600 });
	const [saving, setSaving] = useState(false);
	const [saved, setSaved] = useState<{ ok: boolean; message: string } | null>(null);
	const withModule = hasBluetoothModule(boardId);
	const pins = useMemo(
		() =>
			(findBoard(boardId)?.pins ?? [])
				.filter((pin) => pin.capabilities.includes("digital") && !USB_PINS.includes(pin.label))
				.map((pin) => pin.label),
		[boardId]
	);

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

	const save = async (wanted: BluetoothModule | null) => {
		if (!usbPort) return;
		setSaving(true);
		setSaved(null);
		try {
			const result = await bridge.bluetoothModule(usbPort, boardId, wanted, language);
			setSaved({ ok: result.ok, message: result.message });
		} catch (error) {
			setSaved({ ok: false, message: error instanceof Error ? error.message : String(error) });
		} finally {
			setSaving(false);
		}
	};

	const searching = search.state === "searching";
	const busy = searching || saving;
	return (
		<Modal
			opened={opened}
			onClose={busy ? () => undefined : onClose}
			title={
				<Group gap="xs">
					<IconBluetooth size={20} />
					<Text fw={700}>{t(withModule ? "bluetooth.module_title" : "bluetooth.title")}</Text>
				</Group>
			}
			size="lg"
		>
			<Stack gap="sm">
				{withModule ? (
					<>
						<Title order={5}>{t("bluetooth.module_wiring_title")}</Title>
						<Text size="sm">{t("bluetooth.module_wiring", { rx: module.rx, tx: module.tx })}</Text>
						<Group grow align="flex-end">
							<Select
								label={t("bluetooth.module_rx")}
								data={pins}
								value={module.rx}
								allowDeselect={false}
								onChange={(value) => value && setModule({ ...module, rx: value })}
							/>
							<Select
								label={t("bluetooth.module_tx")}
								data={pins}
								value={module.tx}
								allowDeselect={false}
								onChange={(value) => value && setModule({ ...module, tx: value })}
							/>
							<Select
								label={t("bluetooth.module_speed")}
								data={SPEEDS}
								value={String(module.baud)}
								allowDeselect={false}
								onChange={(value) => value && setModule({ ...module, baud: Number(value) })}
							/>
						</Group>
						<Text size="sm" c="dimmed">
							{t("bluetooth.module_save_help")}
						</Text>
						{!usbPort && (
							<Alert color="yellow" variant="light">
								{t("bluetooth.module_no_usb")}
							</Alert>
						)}
						<Group>
							<Button loading={saving} disabled={!usbPort || searching} onClick={() => void save(module)}>
								{t("bluetooth.module_save")}
							</Button>
							<Button variant="subtle" color="gray" disabled={!usbPort || busy} onClick={() => void save(null)}>
								{t("bluetooth.module_forget")}
							</Button>
						</Group>
						{saved && (
							<Alert color={saved.ok ? "green" : "orange"} variant="light">
								{saved.message}
							</Alert>
						)}
						<Divider />
						<Title order={5}>{t("bluetooth.module_pair_title")}</Title>
						<Text size="sm">{t("bluetooth.module_pair")}</Text>
					</>
				) : (
					<>
						<Text size="sm" c="dimmed">
							{t("bluetooth.first_time")}
						</Text>
						<Text size="sm">{t("bluetooth.help")}</Text>
					</>
				)}
				<Group>
					<Button
						loading={searching}
						disabled={saving}
						onClick={() => void start()}
						leftSection={<IconBluetooth size={16} />}
					>
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
						{t(withModule ? "bluetooth.module_none" : "bluetooth.none")}
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
						{t(withModule ? "bluetooth.module_mac_help" : "bluetooth.mac_help")}
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
