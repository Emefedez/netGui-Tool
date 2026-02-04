#include "tui_app.h"
#include "ethernet.h"
#include "netgui_actions.h"

#include <ncurses.h>

#include <algorithm>
#include <cerrno>
#include <poll.h>
#include <filesystem>
#include <system_error>
#include <string>
#include <vector>

namespace {
struct LogBuffer {
    std::vector<std::string> lines;
    std::size_t maxLines = 200;

    void push(const std::string& line) {
        lines.push_back(line);
        if (lines.size() > maxLines) {
            lines.erase(lines.begin(), lines.begin() + (lines.size() - maxLines));
        }
    }
};

void drawHeader(WINDOW* win, const std::string& iface, const std::string& status) {
    werase(win);
    box(win, 0, 0);
    mvwprintw(win, 1, 2, "NetGui-Tool (TUI ncurses)");
    mvwprintw(win, 2, 2, "Interfaz: %s", iface.c_str());
    mvwprintw(win, 3, 2, "Estado: %s", status.c_str());
    wrefresh(win);
}

void drawFooter(WINDOW* win) {
    werase(win);
    box(win, 0, 0);
    mvwprintw(win, 1, 2, "[s] Demo(0x00)  [d] Demo(0xFF)  [c] Enviar custom  [e] Editar custom");
    mvwprintw(win, 2, 2, "[r] Recargar custom  [i] Info  [q] Salir  [Up/Down PgUp/PgDn] Scroll");
    wrefresh(win);
}

void drawScrollBar(WINDOW* win, int totalLines, int maxLines, int scrollOffset) {
    int h, w;
    getmaxyx(win, h, w);
    const int barTop = 1;
    const int barBottom = h - 2;
    if (barBottom <= barTop) return;

    const int barHeight = barBottom - barTop + 1;
    if (totalLines <= maxLines) {
        for (int y = barTop; y <= barBottom; ++y) {
            mvwaddch(win, y, w - 2, ' ');
        }
        return;
    }

    const int maxStart = totalLines - maxLines;
    if (scrollOffset < 0) scrollOffset = 0;
    if (scrollOffset > maxStart) scrollOffset = maxStart;

    for (int y = barTop; y <= barBottom; ++y) {
        mvwaddch(win, y, w - 2, ACS_VLINE);
    }

    const int knobPos = barTop + (barHeight - 1) * scrollOffset / maxStart;
    mvwaddch(win, knobPos, w - 2, ACS_DIAMOND);
}

void drawLogLine(WINDOW* win, int y, int x, int maxWidth, const std::string& line) {
    auto addSegment = [&](const std::string& text, int colorPair) {
        if (maxWidth <= 0 || text.empty()) return;
        if (colorPair > 0) wattron(win, COLOR_PAIR(colorPair));
        const int n = std::min(static_cast<int>(text.size()), maxWidth);
        waddnstr(win, text.c_str(), n);
        maxWidth -= n;
        if (colorPair > 0) wattroff(win, COLOR_PAIR(colorPair));
    };

    wmove(win, y, x);

    std::string tag;
    int tagColor = 0;
    if (line.rfind("[RX]", 0) == 0) { tag = "[RX]"; tagColor = 1; }
    else if (line.rfind("[TX]", 0) == 0) { tag = "[TX]"; tagColor = 2; }
    else if (line.rfind("[INFO]", 0) == 0) { tag = "[INFO]"; tagColor = 3; }
    else if (line.rfind("[WARN]", 0) == 0) { tag = "[WARN]"; tagColor = 4; }

    std::string body = line;
    if (!tag.empty()) {
        addSegment(tag, tagColor);
        body = line.substr(tag.size());
    }

    const std::string protoKey = " proto=";
    const std::size_t protoPos = body.find(protoKey);
    if (protoPos != std::string::npos) {
        addSegment(body.substr(0, protoPos), 0);
        addSegment(protoKey, 0);
        addSegment(body.substr(protoPos + protoKey.size()), 3);
    } else {
        addSegment(body, 0);
    }
}

void drawLog(WINDOW* win, const LogBuffer& log, int scrollOffset) {
    werase(win);
    box(win, 0, 0);
    int h, w;
    getmaxyx(win, h, w);
    const int maxLines = h - 2;
    int start = 0;
    const int total = static_cast<int>(log.lines.size());
    const int maxStart = (total > maxLines) ? (total - maxLines) : 0;
    if (scrollOffset > maxStart) scrollOffset = maxStart;
    if (scrollOffset < 0) scrollOffset = 0;
    start = maxStart - scrollOffset;
    for (int i = 0; i < maxLines && (start + i) < static_cast<int>(log.lines.size()); ++i) {
        const std::string& line = log.lines[start + i];
        drawLogLine(win, 1 + i, 2, w - 5, line);
    }
    drawScrollBar(win, total, maxLines, scrollOffset);
    wrefresh(win);
}

std::string txResult(int sent) {
    if (sent > 0) {
        return "TX OK (" + std::to_string(sent) + " bytes)";
    }
    return "TX ERROR";
}

std::string etherTypeLabel(std::uint16_t etherType) {
    switch (etherType) {
        case EtherType::IPv4: return "IPv4";
        case EtherType::ARP: return "ARP";
        case EtherType::IPv6: return "IPv6";
        case EtherType::Demo: return "DEMO";
        default: return "OTRO";
    }
}

void drawInfo(WINDOW* win) {
    werase(win);
    box(win, 0, 0);
    mvwprintw(win, 1, 2, "Info - Modo TUI");
    mvwprintw(win, 3, 2, "TX (rojo): paquetes enviados al kernel (write).");
    mvwprintw(win, 4, 2, "RX (verde): paquetes capturados desde el kernel (read).");
    mvwprintw(win, 6, 2, "EtherType: IPv4/ARP/IPv6/DEMO. Otros = OTRO.");
    mvwprintw(win, 7, 2, "Payload: bytes despues de los 14B de cabecera Ethernet.");
    mvwprintw(win, 9, 2, "Controles: s/d demo, c custom, e editar, r recargar, q salir.");
    mvwprintw(win, 10, 2, "Scroll logs: Flechas Arriba/Abajo o PgUp/PgDn.");
    wrefresh(win);
}
} // namespace

int runTuiApp(TapDevice& tap) {
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);
    curs_set(0);
    if (has_colors()) {
        start_color();
        init_pair(1, COLOR_GREEN, COLOR_BLACK); // RX
        init_pair(2, COLOR_RED, COLOR_BLACK);   // TX
        init_pair(3, COLOR_CYAN, COLOR_BLACK);  // INFO
        init_pair(4, COLOR_YELLOW, COLOR_BLACK);// WARN
    }

    int termH, termW;
    getmaxyx(stdscr, termH, termW);
    int headerH = 5;
    int footerH = 4;
    int logH = termH - headerH - footerH;
    if (logH < 4) logH = 4;

    WINDOW* headerWin = newwin(headerH, termW, 0, 0);
    WINDOW* logWin = newwin(logH, termW, headerH, 0);
    WINDOW* footerWin = newwin(footerH, termW, headerH + logH, 0);

    LogBuffer log;
    std::string status = "Inicializando";

    std::error_code ec;
    std::filesystem::path basePath = std::filesystem::current_path(ec);
    if (ec) basePath = ".";
    std::filesystem::path packetFile = basePath / "custom_packet.hex";
    std::string msg;
    if (ensureCustomPacketTemplate(packetFile, msg)) {
        log.push("[INFO] " + msg);
    } else {
        log.push("[WARN] " + msg);
    }

    auto customPacket = loadCustomPacket(packetFile);
    if (customPacket) {
        status = "Custom cargado: " + std::to_string(customPacket->size()) + " bytes";
    } else {
        status = "Custom NO cargado (revise " + packetFile.string() + ")";
    }

    std::vector<uint8_t> rxBuffer(2048);

    bool running = true;
    bool showInfo = false;
    int scrollOffset = 0;
    while (running) {
        drawHeader(headerWin, tap.name(), status);
        if (showInfo) {
            drawInfo(logWin);
        } else {
            drawLog(logWin, log, scrollOffset);
        }
        drawFooter(footerWin);

        struct pollfd pfd;
        pfd.fd = tap.getFd();
        pfd.events = POLLIN;
        int ret = poll(&pfd, 1, 100);

        int ch = getch();
        if (ch != ERR) {
            if (ch == 'q' || ch == 'Q') {
                status = "Saliendo...";
                running = false;
            } else if (ch == 'i' || ch == 'I') {
                showInfo = !showInfo;
            } else if (ch == 's' || ch == 'S') {
                auto bytes = serializeEthernetII(makeDefaultDemoFrame(0));
                int sent = tap.write(bytes.data(), bytes.size());
                status = txResult(sent);
                log.push("[TX] Demo 0x00 -> " + status);
            } else if (ch == 'd' || ch == 'D') {
                auto bytes = serializeEthernetII(makeDefaultDemoFrame(1));
                int sent = tap.write(bytes.data(), bytes.size());
                status = txResult(sent);
                log.push("[TX] Demo 0xFF -> " + status);
            } else if (ch == 'e' || ch == 'E') {
                openFileInEditor(packetFile, msg);
                log.push("[INFO] " + msg);
            } else if (ch == 'r' || ch == 'R') {
                customPacket = loadCustomPacket(packetFile);
                if (customPacket) {
                    status = "Custom cargado: " + std::to_string(customPacket->size()) + " bytes";
                    log.push("[INFO] [CUSTOM] Recargado OK");
                } else {
                    status = "Custom inválido";
                    log.push("[WARN] [CUSTOM] Error de parseo");
                }
            } else if (ch == 'c' || ch == 'C') {
                if (!customPacket) {
                    status = "Custom no cargado";
                    log.push("[WARN] [TX] Custom falló: no hay bytes");
                } else {
                    int sent = tap.write(customPacket->data(), customPacket->size());
                    status = txResult(sent);
                    log.push("[TX] Custom -> " + status);
                }
            } else if (ch == KEY_UP) {
                scrollOffset += 1;
            } else if (ch == KEY_DOWN) {
                scrollOffset -= 1;
            } else if (ch == KEY_PPAGE) {
                scrollOffset += 5;
            } else if (ch == KEY_NPAGE) {
                scrollOffset -= 5;
            }
        }

        if (ret > 0 && (pfd.revents & POLLIN)) {
            int n = tap.read(rxBuffer.data(), rxBuffer.size());
            if (n > 0) {
                auto frameOpt = parseEthernetII(rxBuffer.data(), n);
                if (frameOpt) {
                    const std::string typeLabel = etherTypeLabel(frameOpt->etherType);
                    log.push("[RX] " + describeEthernetII(*frameOpt) + " proto=" + typeLabel);
                } else {
                    log.push("[RX] " + std::to_string(n) + " bytes (raw)");
                }
            } else if (n < 0 && errno != EAGAIN) {
                log.push("[RX] Error leyendo TAP");
            }
        }
    }

    delwin(footerWin);
    delwin(logWin);
    delwin(headerWin);
    endwin();
    return 0;
}
