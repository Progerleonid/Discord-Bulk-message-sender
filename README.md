# Discord Bulk Message Sender (Discord Spammer)

[English](#english) | [Русский](#русский)

---

## English

A desktop control panel for automated bulk message distribution on Discord, built with C++.

### 🚀 Features
* **Dual Token Support:** Works with both standard Discord Bot tokens and User account tokens (Self-botting).
* **Automatic Detection:** Built-in "Auto" token type validator.
* **Flexible Target Modes:** Send messages to server channels or directly to private user chats (DMs).
* **Advanced Timing Control:** Adjustable message interval (in seconds) and total message limits to prevent API bans.
* **Turbo Mode:** Toggle high-speed automated delivery.
* **Live Status Log:** Real-time feedback monitor.

### 🛠️ How to Use
1. **Authorization:** 
   * Paste your Bot token or User account token into the **Discord token** field.
   * Select the **Token type** (`Auto`, `Bot`, or `User`) and click **Validate**.
2. **Setup Destination:**
   * Choose **Server** to target specific guilds, then click **Refresh** to fetch accessible servers.
   * Choose **DM** to target direct messages.
   * Use **Load** to populate the **Channels / DMs** list target configuration.
3. **Draft Message:** 
   * Enter your text in the **Message text** area.
   * Adjust **Interval**, **Limit**, or enable **Turbo** mode if needed.
4. **Execute:** 
   * Click **Start** to begin sending.
   * Click **Stop** to pause the process at any time.

---

## Русский

Десктопная панель управления на C++ для автоматизированной массовой рассылки сообщений в Discord.

### 🚀 Возможности
* **Поддержка двух типов токенов:** Работает как с токенами Discord Ботов, так и с токенами обычных аккаунтов пользователей (Self-botting).
* **Автоматическое определение:** Встроенный валидатор типа токена (режим `Auto`).
* **Гибкие режимы отправки:** Рассылка по текстовым каналам серверов или напрямую в личные сообщения пользователей (DM).
* **Контроль задержки:** Настройка интервала между сообщениями (в секундах) и лимита отправки для защиты от банов API.
* **Режим Turbo:** Переключатель для высокоскоростной автоматической отправки.
* **Мониторинг статуса:** Отображение текущего состояния программы в реальном времени.

### 🛠️ Инструкция по использованию
1. **Авторизация:**
   * Вставьте токен бота или токен аккаунта пользователя в поле **Discord token**.
   * Выберите **Token type** (`Auto`, `Bot` или `User`) и нажмите кнопку **Validate**.
2. **Выбор цели:**
   * Выберите **Server** для отправки в текстовые каналы серверов, затем нажмите **Refresh** для обновления списка доступных серверов.
   * Выберите **DM** для отправки в личные сообщения.
   * Используйте кнопку **Load**, чтобы загрузить цели в окно **Channels / DMs**.
3. **Настройка сообщения:**
   * Введите нужный текст в поле **Message text**.
   * Задайте **Interval** (интервал), **Limit** (лимит) или включите режим **Turbo** при необходимости.
4. **Запуск:**
   * Нажмите кнопку **Start** для начала рассылки.
   * Нажмите **Stop**, чтобы остановить процесс в любой момент.
