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
#include <optional>

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
    mvwprintw(win, 1, 2, "[s] Demo(0x00)  [d] Demo(0xFF)  [t] Demo RX  [c] Enviar custom  [e] Editar custom");
    mvwprintw(win, 2, 2, "[r] Recargar custom  [x] Guardar RX  [i] Info  [q] Salir  [Up/Down] Scroll");
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

    const int knobPos = barTop + (barHeight - 1) * (maxStart - scrollOffset) / maxStart;
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

void drawInfo(WINDOW* win, int infoPage = 0) {
    werase(win);
    box(win, 0, 0);
    
    if (infoPage == 0) {
        mvwprintw(win, 1, 2, "Info - Conceptos Basicos (1/3)");
        mvwprintw(win, 3, 2, "TX (rojo): Paquetes que tu programa envia al kernel.");
        mvwprintw(win, 4, 2, "RX (verde): Paquetes que el kernel intenta enviar a la red.");
        mvwprintw(win, 6, 2, "TAP: Interfaz virtual que simula un cable Ethernet.");
        mvwprintw(win, 7, 2, "Trama Ethernet: MAC dst (6B) + MAC src (6B) + EtherType (2B) + Payload");
        mvwprintw(win, 9, 2, "EtherType: Codigo de protocolo. 0x0800=IPv4, 0x0806=ARP, 0x88B5=Demo.");
        mvwprintw(win, 10, 2, "Payload: Datos. Minimo 46 bytes (padding automatico).");
        mvwprintw(win, 12, 2, "Controles: [i] Info  [-] Pagina anterior  [+] Siguiente  [q] Salir");
    } else if (infoPage == 1) {
        mvwprintw(win, 1, 2, "Info - Editar Paquetes Custom (2/3)");
        mvwprintw(win, 3, 2, "Archivo: custom_packet.hex (editar con [e])");
        mvwprintw(win, 4, 2, "Formato: Bytes en hexadecimal, separados por espacios.");
        mvwprintw(win, 5, 2, "Comentarios: # o // al inicio de linea.");
        mvwprintw(win, 7, 2, "Estructura minima (60 bytes):");
        mvwprintw(win, 8, 2, "  ff ff ff ff ff ff    (MAC dest: broadcast)");
        mvwprintw(win, 9, 2, "  02 00 00 00 00 01    (MAC src: fake)");
        mvwprintw(win, 10, 2, "  88 b5                (EtherType: Demo)");
        mvwprintw(win, 11, 2, "  42 00 00... (46 bytes payload minimo)");
        mvwprintw(win, 13, 2, "Ejemplo: MAC 52:54:00:12:34:56 = 52 54 00 12 34 56");
        mvwprintw(win, 14, 2, "Controles: [i] Info  [/] Anterior  [*] Siguiente  [q] Salir");
    } else if (infoPage == 2) {
        mvwprintw(win, 1, 2, "Info - Valores de Bits y Ejemplos (3/3)");
        mvwprintw(win, 3, 2, "MAC Address (48 bits = 6 bytes):");
        mvwprintw(win, 4, 2, "  ff:ff:ff:ff:ff:ff = Broadcast (todos los dispositivos)");
        mvwprintw(win, 5, 2, "  00:11:22:33:44:55 = Unicast (dispositivo especifico)");
        mvwprintw(win, 7, 2, "EtherType (16 bits = 2 bytes, Big Endian):");
        mvwprintw(win, 8, 2, "  0x0800 = IPv4  | 0x0806 = ARP | 0x86DD = IPv6 | 0x88B5 = Demo");
        mvwprintw(win, 10, 2, "Payload (variable, minimo 46 bytes):");
        mvwprintw(win, 11, 2, "  Bytes arbitrarios (data util del protocolo)");
        mvwprintw(win, 12, 2, "  42 = 'B' en ASCII, util para patrones visibles");
        mvwprintw(win, 14, 2, "Controles: [i] Info  [/] Anterior  [*] Siguiente  [q] Salir");
    }
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
    int infoPage = 0;
    int scrollOffset = 0;
    std::optional<EthernetFrame> lastRxFrame;
    while (running) {
        drawHeader(headerWin, tap.name(), status);
        if (showInfo) {
            drawInfo(logWin, infoPage);
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
                infoPage = 0;
            } else if (ch == '-' && showInfo) {
                infoPage = (infoPage - 1 + 3) % 3;
            } else if (ch == '+' && showInfo) {
                infoPage = (infoPage + 1) % 3;
            } else if (ch == 's' || ch == 'S') {
                auto frame = makeDefaultDemoFrame(0);
                auto bytes = serializeEthernetII(frame);
                int sent = tap.write(bytes.data(), bytes.size());
                status = txResult(sent);
                log.push("[TX] Demo 0x00 (" + std::to_string(bytes.size()) + "B) -> " + status);
            } else if (ch == 'd' || ch == 'D') {
                auto frame = makeDefaultDemoFrame(1);
                auto bytes = serializeEthernetII(frame);
                int sent = tap.write(bytes.data(), bytes.size());
                status = txResult(sent);
                log.push("[TX] Demo 0xFF (" + std::to_string(bytes.size()) + "B) -> " + status);
            } else if (ch == 'e' || ch == 'E') {
                endwin();
                openFileInEditor(packetFile, msg);
                initscr();
                cbreak();
                noecho();
                keypad(stdscr, TRUE);
                nodelay(stdscr, TRUE);
                curs_set(0);
                if (has_colors()) {
                    start_color();
                    init_pair(1, COLOR_GREEN, COLOR_BLACK);
                    init_pair(2, COLOR_RED, COLOR_BLACK);
                    init_pair(3, COLOR_CYAN, COLOR_BLACK);
                    init_pair(4, COLOR_YELLOW, COLOR_BLACK);
                }
                log.push(msg);
                customPacket = loadCustomPacket(packetFile);
                if (customPacket) {
                    status = "Custom editado y recargado: " + std::to_string(customPacket->size()) + " bytes";
                } else {
                    status = "Error al parsear custom editado";
                }
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
            } else if (ch == 'x' || ch == 'X') {
                if (!lastRxFrame) {
                    log.push("[WARN] [RX] No hay paquete RX capturado para guardar");
                } else {
                    if (saveRxFrameAsCustom(*lastRxFrame, packetFile, msg)) {
                        log.push(msg);
                        customPacket = loadCustomPacket(packetFile);
                        status = "RX guardado como custom";
                    } else {
                        log.push(msg);
                        status = "Error al guardar RX";
                    }
                }
            } else if (ch == 't' || ch == 'T') {
                auto demoFrame = makeDefaultDemoFrame(0);
                lastRxFrame = demoFrame;
                const std::string typeLabel = etherTypeLabel(demoFrame.etherType);
                log.push("[RX] Demo simulado: " + describeEthernetII(demoFrame) + " proto=" + typeLabel);
                status = "RX Demo simulado (como si fuera del kernel)";
            } else if (ch == KEY_UP) {
                scrollOffset -= 1;
            } else if (ch == KEY_DOWN) {
                scrollOffset += 1;
            } else if (ch == KEY_PPAGE) {
                scrollOffset -= 5;
            } else if (ch == KEY_NPAGE) {
                scrollOffset += 5;
            }
        }

        if (ret > 0 && (pfd.revents & POLLIN)) {
            int n = tap.read(rxBuffer.data(), rxBuffer.size());
            if (n > 0) {
                auto frameOpt = parseEthernetII(rxBuffer.data(), n);
                if (frameOpt) {
                    lastRxFrame = frameOpt;
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
