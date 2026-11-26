"""Receive Discord slash commands: /snapshot /status /on /off /help.

The bot connects OUT to Discord (websocket), so no public IP / port forwarding
is needed. Only the Discord users listed in DISCORD_ALLOWED_USER_IDS may use
the commands; everyone else gets a refusal. Replies are ephemeral (only the
person who typed the command sees them, including the camera picture).

This is separate from DISCORD_WEBHOOK_URL: the webhook only POSTS alerts into a
channel, the bot is what understands commands.
"""

import asyncio
import os
import re
import threading

import discord
from discord import app_commands

RETRY_MIN = 5  # seconds before reconnecting after an error
RETRY_MAX = 300

HELP_TEXT = (
    "ESP32-CAM Security Camera\n\n"
    "/snapshot - Chup anh hien tai\n"
    "/on - Bat phat hien chuyen dong\n"
    "/off - Tat phat hien chuyen dong\n"
    "/status - Xem trang thai he thong\n"
    "/help - Huong dan su dung"
)

NO_PERMISSION = "Ban khong co quyen dung lenh nay."
SNAPSHOT_FAILED = "Khong the chup anh: camera offline hoac chua co frame."


class DiscordBot:
    def __init__(self, controller, log=print):
        """controller: system_controller.SystemController (shared with Telegram)."""
        self.controller = controller
        self.log = log

        self.token = os.getenv("DISCORD_BOT_TOKEN", "").strip()
        self.guild_id = os.getenv("DISCORD_GUILD_ID", "").strip()
        self.allowed_user_ids = self._parse_ids(
            os.getenv("DISCORD_ALLOWED_USER_IDS", "")
        )

        if not self.token:
            raise ValueError("Missing DISCORD_BOT_TOKEN")

        if not self.allowed_user_ids:
            raise ValueError(
                "Missing DISCORD_ALLOWED_USER_IDS (your Discord user id)"
            )

        if self.guild_id and not self.guild_id.isdigit():
            raise ValueError("DISCORD_GUILD_ID must be a number")

        self.stop_event = threading.Event()
        self._loop = None
        self._client = None

    @staticmethod
    def _parse_ids(text):
        ids = set()

        for part in re.split(r"[,\s]+", text.strip()):
            if not part:
                continue

            if not part.isdigit():
                raise ValueError(
                    "DISCORD_ALLOWED_USER_IDS must be numbers separated by commas"
                )

            ids.add(int(part))

        return ids

    # ------------------------------------------------------------ commands

    def _authorized(self, interaction):
        return interaction.user.id in self.allowed_user_ids

    async def _deny(self, interaction, name):
        self.log(f"[discord] Ignored /{name} from an unauthorized user")
        await interaction.response.send_message(NO_PERMISSION, ephemeral=True)

    async def cmd_snapshot(self, interaction):
        if not self._authorized(interaction):
            return await self._deny(interaction, "snapshot")

        self.log("[discord] Received command: /snapshot")

        # Discord wants an answer within 3 s: acknowledge first
        await interaction.response.defer(ephemeral=True, thinking=True)
        path = await asyncio.to_thread(self.controller.take_snapshot)

        if path is None:
            await interaction.followup.send(SNAPSHOT_FAILED, ephemeral=True)
            return

        await interaction.followup.send(
            "Anh chup thu cong tu ESP32-CAM",
            file=discord.File(str(path)),
            ephemeral=True,
        )
        self.log("[discord] Snapshot sent")

    async def cmd_status(self, interaction):
        if not self._authorized(interaction):
            return await self._deny(interaction, "status")

        self.log("[discord] Received command: /status")
        text = await asyncio.to_thread(self.controller.status_text)
        await interaction.response.send_message(text, ephemeral=True)

    async def cmd_motion(self, interaction, enabled):
        name = "on" if enabled else "off"

        if not self._authorized(interaction):
            return await self._deny(interaction, name)

        self.log(f"[discord] Received command: /{name}")
        text = await asyncio.to_thread(self.controller.set_motion, enabled)
        await interaction.response.send_message(text, ephemeral=True)

    async def cmd_help(self, interaction):
        if not self._authorized(interaction):
            return await self._deny(interaction, "help")

        await interaction.response.send_message(HELP_TEXT, ephemeral=True)

    async def on_command_error(self, interaction, error):
        self.log(
            f"[discord] Command error: "
            f"{type(getattr(error, 'original', error)).__name__}"
        )

        try:
            if interaction.response.is_done():
                await interaction.followup.send("Loi noi bo.", ephemeral=True)
            else:
                await interaction.response.send_message(
                    "Loi noi bo.", ephemeral=True
                )
        except Exception:
            pass

    # ---------------------------------------------------------- connection

    def _guild(self):
        return discord.Object(id=int(self.guild_id)) if self.guild_id else None

    def build_commands(self):
        """The slash commands (a list of app_commands.Command)."""
        bot = self

        async def snapshot(interaction: discord.Interaction):
            await bot.cmd_snapshot(interaction)

        async def status(interaction: discord.Interaction):
            await bot.cmd_status(interaction)

        async def on(interaction: discord.Interaction):
            await bot.cmd_motion(interaction, True)

        async def off(interaction: discord.Interaction):
            await bot.cmd_motion(interaction, False)

        async def help_(interaction: discord.Interaction):
            await bot.cmd_help(interaction)

        return [
            app_commands.Command(
                name="snapshot",
                description="Chup anh hien tai tu ESP32-CAM",
                callback=snapshot,
            ),
            app_commands.Command(
                name="status",
                description="Xem trang thai he thong",
                callback=status,
            ),
            app_commands.Command(
                name="on",
                description="Bat phat hien chuyen dong",
                callback=on,
            ),
            app_commands.Command(
                name="off",
                description="Tat phat hien chuyen dong",
                callback=off,
            ),
            app_commands.Command(
                name="help",
                description="Huong dan su dung",
                callback=help_,
            ),
        ]

    async def _run_once(self):
        bot = self
        guild = self._guild()

        class _Client(discord.Client):
            async def setup_hook(self):
                # Runs after login, before the gateway connection
                synced = await bot._sync(self.tree)
                bot.log(f"[discord] Registered {synced} slash commands")

            async def on_ready(self):
                bot.log(f"[discord] Bot connected as {self.user}")

        client = _Client(intents=discord.Intents.default())
        client.tree = app_commands.CommandTree(client)

        for command in self.build_commands():
            client.tree.add_command(command, guild=guild)

        client.tree.on_error = self.on_command_error

        self._loop = asyncio.get_running_loop()
        self._client = client

        async with client:
            await client.start(self.token)

    async def _sync(self, tree):
        """Send the command list to Discord. A guild sync is instant, a global
        one can take up to an hour to show up."""
        return len(await tree.sync(guild=self._guild()))

    def run_forever(self):
        """Thread target: keep the bot connected until stop() is called."""
        self.log("[discord] Bot starting")
        wait = RETRY_MIN

        while not self.stop_event.is_set():
            try:
                asyncio.run(self._run_once())
            except discord.LoginFailure:
                self.log(
                    "[discord] Login failed: DISCORD_BOT_TOKEN is wrong or "
                    "was reset. Bot stopped."
                )
                return
            except Exception as exc:
                # Log the type only: never print anything that could hold the token
                self.log(
                    f"[discord] Bot error: {type(exc).__name__}, "
                    f"retry in {wait}s"
                )
                self.stop_event.wait(wait)
                wait = min(wait * 2, RETRY_MAX)
                continue

            # The client closed without an error (e.g. stop()): do not spin
            wait = RETRY_MIN
            self.stop_event.wait(RETRY_MIN)

        self.log("[discord] Bot stopped")

    def stop(self):
        """Ask the bot to disconnect and the thread to finish."""
        self.stop_event.set()
        loop, client = self._loop, self._client

        if loop is not None and client is not None and not loop.is_closed():
            asyncio.run_coroutine_threadsafe(client.close(), loop)
