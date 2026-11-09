import { Alert, Button, Code, Divider, Group, Modal, PasswordInput, Stack, Text, TextInput, Title } from "@mantine/core";
import { IconWifi } from "@tabler/icons-react";
import { useState } from "react";

import { bridge, type WifiSetup } from "@/bridge";
import type { Language } from "@/catalog";
import type { MessageKey } from "@/i18n";
import { ACCESS_POINT_PASSWORD } from "@/wifi";

interface WifiDialogProps {
	opened: boolean;
	language: Language;
	boardId: string;
	/** The board's USB port (the home network is sent over the cable), null when none. */
	usbPort: string | null;
	t(key: MessageKey, params?: Record<string, string>): string;
	/** The board joined the home network at this address. */
	onJoined(address: string): void;
	onClose(): void;
}

/** How to reach an ESP32 over Wi-Fi: its own network, or the home network sent over USB. */
export function WifiDialog({ opened, language, boardId, usbPort, t, onJoined, onClose }: WifiDialogProps) {
	const [name, setName] = useState("");
	const [password, setPassword] = useState("");
	const [busy, setBusy] = useState(false);
	const [result, setResult] = useState<{ ok: boolean; message: string } | null>(null);

	const send = async (networkName: string, networkPassword: string) => {
		if (!usbPort) return;
		setBusy(true);
		setResult(null);
		try {
			const setup: WifiSetup = await bridge.wifiSetup(usbPort, networkName, networkPassword, boardId, language);
			setResult({ ok: setup.ok, message: setup.message });
			if (setup.ok && setup.address) onJoined(setup.address);
		} catch (error) {
			setResult({ ok: false, message: error instanceof Error ? error.message : String(error) });
		} finally {
			setBusy(false);
		}
	};

	return (
		<Modal
			opened={opened}
			onClose={busy ? () => undefined : onClose}
			title={
				<Group gap="xs">
					<IconWifi size={20} />
					<Text fw={700}>{t("wifi.title")}</Text>
				</Group>
			}
			size="lg"
		>
			<Stack gap="sm">
				<Text size="sm" c="dimmed">
					{t("wifi.first_time")}
				</Text>
				<Title order={5}>{t("wifi.access_point_title")}</Title>
				<Text size="sm">{t("wifi.access_point_help")}</Text>
				<Group gap="xs">
					<Text size="sm">{t("wifi.password")} :</Text>
					<Code>{ACCESS_POINT_PASSWORD}</Code>
				</Group>
				<Divider my="xs" />
				<Title order={5}>{t("wifi.home_title")}</Title>
				<Text size="sm">{t("wifi.home_help")}</Text>
				<TextInput
					label={t("wifi.name")}
					value={name}
					onChange={(event) => setName(event.currentTarget.value)}
					disabled={busy}
					autoComplete="off"
				/>
				<PasswordInput
					label={t("wifi.password")}
					value={password}
					onChange={(event) => setPassword(event.currentTarget.value)}
					disabled={busy}
				/>
				{!usbPort && (
					<Alert color="yellow" variant="light">
						{t("wifi.no_usb")}
					</Alert>
				)}
				<Group justify="space-between">
					<Button variant="subtle" color="gray" disabled={!usbPort || busy} onClick={() => void send("", "")}>
						{t("wifi.forget")}
					</Button>
					<Button loading={busy} disabled={!usbPort || name.trim() === ""} onClick={() => void send(name.trim(), password)}>
						{t("wifi.send")}
					</Button>
				</Group>
				{busy && (
					<Text size="sm" c="dimmed">
						{t("wifi.sending")}
					</Text>
				)}
				{result && (
					<Alert color={result.ok ? "green" : "orange"} variant="light">
						{result.message}
					</Alert>
				)}
			</Stack>
		</Modal>
	);
}
