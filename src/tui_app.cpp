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
    mvwprintw(win, 2, 2, "[r] Recargar  [x] Guardar RX  [b] Toggle TX/RX  [i] Info  [q] Salir  [Up/Down] Scroll");
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

void drawLastTxPanel(WINDOW* win, const std::optional<EthernetFrame>& lastTxFrame) {
    werase(win);
    box(win, 0, 0);
    mvwprintw(win, 0, 2, " Ultimo TX Enviado ");
    
    if (!lastTxFrame) {
        wattron(win, COLOR_PAIR(4));
        mvwprintw(win, 2, 2, "[ Sin paquetes TX ]");
        wattroff(win, COLOR_PAIR(4));
        wrefresh(win);
        return;
    }
    
    int y = 1;
    int h, w;
    getmaxyx(win, h, w);
    
    // Información de cabecera
    wattron(win, COLOR_PAIR(3));
    mvwprintw(win, y++, 2, "Dst: %s", macToString(lastTxFrame->dst).c_str());
    mvwprintw(win, y++, 2, "Src: %s", macToString(lastTxFrame->src).c_str());
    mvwprintw(win, y++, 2, "Tipo: 0x%04X (%s)", lastTxFrame->etherType, etherTypeLabel(lastTxFrame->etherType).c_str());
    wattroff(win, COLOR_PAIR(3));
    y++;
    
    // Payload en hex + ASCII
    mvwprintw(win, y++, 2, "Payload (%zu bytes):", lastTxFrame->payload.size());
    const auto& payload = lastTxFrame->payload;
    const int bytesPerLine = 16;
    const int maxLines = h - y - 2; // Espacio disponible
    
    for (std::size_t i = 0; i < payload.size() && (y - 6) < maxLines; i += bytesPerLine) {
        // Offset
        wattron(win, COLOR_PAIR(4));
        mvwprintw(win, y, 2, "%04zX", i);
        wattroff(win, COLOR_PAIR(4));
        
        // Hex bytes
        int x = 7;
        for (std::size_t j = 0; j < bytesPerLine && (i + j) < payload.size(); ++j) {
            mvwprintw(win, y, x, "%02X", payload[i + j]);
            x += 3;
        }
        
        // Parte ASCII
        x = 7 + bytesPerLine * 3 + 2;
        if (x + bytesPerLine < w - 2) {
            wattron(win, COLOR_PAIR(3));
            for (std::size_t j = 0; j < bytesPerLine && (i + j) < payload.size(); ++j) {
                std::uint8_t byte = payload[i + j];
                char ch = (byte >= 32 && byte <= 126) ? static_cast<char>(byte) : '.';
                mvwaddch(win, y, x + j, ch);
            }
            wattroff(win, COLOR_PAIR(3));
        }
        
        y++;
    }
    
    // Indicador si hay más datos
    if (payload.size() > static_cast<std::size_t>(maxLines * bytesPerLine)) {
        wattron(win, COLOR_PAIR(4));
        mvwprintw(win, y, 2, "... (%zu bytes mas)", 
                 payload.size() - (maxLines * bytesPerLine));
        wattroff(win, COLOR_PAIR(4));
    }
    
    wrefresh(win);
}

void drawLastRxPanel(WINDOW* win, const std::optional<EthernetFrame>& lastRxFrame) {
    werase(win);
    box(win, 0, 0);
    mvwprintw(win, 0, 2, " Ultimo RX Capturado ");
    
    if (!lastRxFrame) {
        wattron(win, COLOR_PAIR(4));
        mvwprintw(win, 2, 2, "[ Sin paquetes RX ]");
        wattroff(win, COLOR_PAIR(4));
        wrefresh(win);
        return;
    }
    
    int y = 1;
    int h, w;
    getmaxyx(win, h, w);
    
    // Información de cabecera
    wattron(win, COLOR_PAIR(3));
    mvwprintw(win, y++, 2, "Dst: %s", macToString(lastRxFrame->dst).c_str());
    mvwprintw(win, y++, 2, "Src: %s", macToString(lastRxFrame->src).c_str());
    mvwprintw(win, y++, 2, "Tipo: 0x%04X (%s)", lastRxFrame->etherType, etherTypeLabel(lastRxFrame->etherType).c_str());
    wattroff(win, COLOR_PAIR(3));
    y++;
    
    // Payload en hex + ASCII
    mvwprintw(win, y++, 2, "Payload (%zu bytes):", lastRxFrame->payload.size());
    const auto& payload = lastRxFrame->payload;
    const int bytesPerLine = 16;
    const int maxLines = h - y - 2; // Espacio disponible
    
    for (std::size_t i = 0; i < payload.size() && (y - 6) < maxLines; i += bytesPerLine) {
        // Offset
        wattron(win, COLOR_PAIR(4));
        mvwprintw(win, y, 2, "%04zX", i);
        wattroff(win, COLOR_PAIR(4));
        
        // Hex bytes
        int x = 7;
        for (std::size_t j = 0; j < bytesPerLine && (i + j) < payload.size(); ++j) {
            mvwprintw(win, y, x, "%02X", payload[i + j]);
            x += 3;
        }
        
        // Parte ASCII
        x = 7 + bytesPerLine * 3 + 2;
        if (x + bytesPerLine < w - 2) {
            wattron(win, COLOR_PAIR(3));
            for (std::size_t j = 0; j < bytesPerLine && (i + j) < payload.size(); ++j) {
                std::uint8_t byte = payload[i + j];
                char ch = (byte >= 32 && byte <= 126) ? static_cast<char>(byte) : '.';
                mvwaddch(win, y, x + j, ch);
            }
            wattroff(win, COLOR_PAIR(3));
        }
        
        y++;
    }
    
    // Indicador si hay más datos
    if (payload.size() > static_cast<std::size_t>(maxLines * bytesPerLine)) {
        wattron(win, COLOR_PAIR(4));
        mvwprintw(win, y, 2, "... (%zu bytes mas)", 
                 payload.size() - (maxLines * bytesPerLine));
        wattroff(win, COLOR_PAIR(4));
    }
    
    wrefresh(win);
}

void drawFrameBreakdown(WINDOW* win, const std::optional<EthernetFrame>& lastFrame, bool isTx) {
    werase(win);
    box(win, 0, 0);
    const char* title = isTx ? " Desglose Trama TX " : " Desglose Trama RX ";
    int titleColor = isTx ? 2 : 1;
    wattron(win, COLOR_PAIR(titleColor));
    mvwprintw(win, 0, 2, "%s", title);
    wattroff(win, COLOR_PAIR(titleColor));
    
    if (!lastFrame) {
        wattron(win, COLOR_PAIR(4));
        mvwprintw(win, 1, 2, "[ Sin paquete ]");
        wattroff(win, COLOR_PAIR(4));
        wrefresh(win);
        return;
    }
    
    // Serializar la trama completa
    auto frameBytes = serializeEthernetII(*lastFrame);
    
    int y = 1;
    int h, w;
    getmaxyx(win, h, w);
    
    mvwprintw(win, y++, 2, "Trama completa: %zu bytes", frameBytes.size());
    y++;
    
    // Mostrar bytes con colores por sección
    const int bytesPerLine = 24;
    std::size_t offset = 0;
    
    // Sección 1: MAC Destino (6 bytes) - Amarillo
    mvwprintw(win, y++, 2, "MAC Destino (6 bytes):");
    wattron(win, COLOR_PAIR(4));
    for (int i = 0; i < 6 && offset < frameBytes.size(); ++i, ++offset) {
        mvwprintw(win, y, 2 + i * 3, "%02X", frameBytes[offset]);
    }
    wattroff(win, COLOR_PAIR(4));
    mvwprintw(win, y++, 21, " = %s", macToString(lastFrame->dst).c_str());
    y++;
    
    // Sección 2: MAC Origen (6 bytes) - Cyan
    mvwprintw(win, y++, 2, "MAC Origen (6 bytes):");
    wattron(win, COLOR_PAIR(3));
    for (int i = 0; i < 6 && offset < frameBytes.size(); ++i, ++offset) {
        mvwprintw(win, y, 2 + i * 3, "%02X", frameBytes[offset]);
    }
    wattroff(win, COLOR_PAIR(3));
    mvwprintw(win, y++, 21, " = %s", macToString(lastFrame->src).c_str());
    y++;
    
    // Sección 3: EtherType (2 bytes) - Magenta (usaremos COLOR_PAIR(2) en rojo)
    init_pair(5, COLOR_MAGENTA, COLOR_BLACK);
    mvwprintw(win, y++, 2, "EtherType (2 bytes):");
    wattron(win, COLOR_PAIR(5));
    for (int i = 0; i < 2 && offset < frameBytes.size(); ++i, ++offset) {
        mvwprintw(win, y, 2 + i * 3, "%02X", frameBytes[offset]);
    }
    wattroff(win, COLOR_PAIR(5));
    mvwprintw(win, y++, 10, " = 0x%04X (%s)", lastFrame->etherType, etherTypeLabel(lastFrame->etherType).c_str());
    y++;
    
    // Sección 4: Payload (resto) - Verde claro
    if (y < h - 3) {
        mvwprintw(win, y++, 2, "Payload (%zu bytes):", lastFrame->payload.size());
        const int maxPayloadLines = h - y - 2;
        int lineCount = 0;
        
        wattron(win, COLOR_PAIR(1));
        for (std::size_t i = 0; i < lastFrame->payload.size() && lineCount < maxPayloadLines; ) {
            int x = 2;
            for (int j = 0; j < bytesPerLine && i < lastFrame->payload.size() && x < w - 3; ++j, ++i) {
                mvwprintw(win, y, x, "%02X", lastFrame->payload[i]);
                x += 3;
            }
            y++;
            lineCount++;
        }
        wattroff(win, COLOR_PAIR(1));
        
        if (lastFrame->payload.size() > static_cast<std::size_t>(maxPayloadLines * bytesPerLine)) {
            wattron(win, COLOR_PAIR(4));
            mvwprintw(win, y, 2, "... (mas bytes)");
            wattroff(win, COLOR_PAIR(4));
        }
    }
    
    wrefresh(win);
}

void drawInfo(WINDOW* win, int infoPage = 0) {
    werase(win);
    box(win, 0, 0);
    
    if (infoPage == 0) {
        mvwprintw(win, 1, 2, "Info - Conceptos Basicos: RX vs TX (1/3)");
        mvwprintw(win, 3, 2, "*** IMPORTANTE: Direccionalidad desde perspectiva del PROGRAMA ***");
        mvwprintw(win, 5, 2, "TX (rojo): Paquetes que tu programa ESCRIBE al kernel (tap.write)");
        mvwprintw(win, 6, 2, "  -> El kernel los RECIBE como si vinieran de la red fisica.");
        mvwprintw(win, 7, 2, "  -> Uso: Inyectar trafico simulado (ej: ARP reply falso, ICMP).");
        mvwprintw(win, 9, 2, "RX (verde): Paquetes que el kernel ENVIA a la red (tap.read captura)");
        mvwprintw(win, 10, 2, "  -> Tu programa los LEE como sniffing del trafico saliente.");
        mvwprintw(win, 11, 2, "  -> Uso: Capturar trafico generado por el SO (ej: ping, curl).");
        mvwprintw(win, 13, 2, "TAP: Interfaz virtual que simula un cable Ethernet de capa 2.");
        mvwprintw(win, 15, 2, "Trama Ethernet: MAC dst (6B) + MAC src (6B) + EtherType (2B) + Payload");
        mvwprintw(win, 16, 2, "EtherType: 0x0800=IPv4, 0x0806=ARP, 0x86DD=IPv6, 0x88B5=Demo.");
        mvwprintw(win, 18, 2, "Controles: [i] Info  [-] Pagina anterior  [+] Siguiente  [q] Salir");
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
        mvwprintw(win, 14, 2, "Controles: [i] Info  [-] Anterior  [+] Siguiente  [q] Salir");
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
        mvwprintw(win, 14, 2, "Controles: [i] Info  [-] Anterior  [+] Siguiente  [q] Salir");
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
    
    // Crear panel de desglose debajo del header si hay espacio
    int breakdownH = 0;
    WINDOW* breakdownWin = nullptr;
    if (termH > 35) {  // Solo si hay suficiente altura
        breakdownH = 18;
    }
    
    int logH = termH - headerH - footerH - breakdownH;
    if (logH < 4) logH = 4;

    // Crear paneles laterales para TX/RX si hay espacio suficiente
    int sidePanelW = 0;
    int logW = termW;
    WINDOW* txPanelWin = nullptr;
    WINDOW* rxPanelWin = nullptr;
    
    if (termW > 150) {  // Espacio para ambos paneles
        sidePanelW = 55;
        logW = termW - (sidePanelW * 2) - 2;
        txPanelWin = newwin(logH, sidePanelW, headerH + breakdownH, 0);
        rxPanelWin = newwin(logH, sidePanelW, headerH + breakdownH, termW - sidePanelW);
    } else if (termW > 100) {  // Espacio solo para panel RX
        sidePanelW = 55;
        logW = termW - sidePanelW - 1;
        rxPanelWin = newwin(logH, sidePanelW, headerH + breakdownH, logW + 1);
    }

    WINDOW* headerWin = newwin(headerH, termW, 0, 0);
    if (breakdownH > 0) {
        breakdownWin = newwin(breakdownH, termW, headerH, 0);
    }
    int logX = (txPanelWin != nullptr) ? sidePanelW + 1 : 0;
    WINDOW* logWin = newwin(logH, logW, headerH + breakdownH, logX);
    WINDOW* footerWin = newwin(footerH, termW, headerH + breakdownH + logH, 0);

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
    std::optional<EthernetFrame> lastTxFrame;
    bool showTxBreakdown = true;  // Por defecto mostrar TX en desglose
    while (running) {
        drawHeader(headerWin, tap.name(), status);
        if (breakdownWin) {
            drawFrameBreakdown(breakdownWin, showTxBreakdown ? lastTxFrame : lastRxFrame, showTxBreakdown);
        }
        if (showInfo) {
            drawInfo(logWin, infoPage);
        } else {
            drawLog(logWin, log, scrollOffset);
        }
        if (txPanelWin) {
            drawLastTxPanel(txPanelWin, lastTxFrame);
        }
        if (rxPanelWin) {
            drawLastRxPanel(rxPanelWin, lastRxFrame);
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
                lastTxFrame = frame;
                showTxBreakdown = true;
                auto bytes = serializeEthernetII(frame);
                int sent = tap.write(bytes.data(), bytes.size());
                status = txResult(sent);
                log.push("[TX] Demo 0x00 (" + std::to_string(bytes.size()) + "B) -> " + status);
            } else if (ch == 'd' || ch == 'D') {
                auto frame = makeDefaultDemoFrame(1);
                lastTxFrame = frame;
                showTxBreakdown = true;
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
                    // Parsear el custom para guardarlo en lastTxFrame
                    auto customFrameOpt = parseEthernetII(customPacket->data(), customPacket->size());
                    if (customFrameOpt) {
                        lastTxFrame = customFrameOpt;
                        showTxBreakdown = true;
                    }
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
                        // BUGFIX: Recargar el custom después de guardarlo
                        customPacket = loadCustomPacket(packetFile);
                        if (customPacket) {
                            status = "RX guardado y cargado como custom (" + std::to_string(customPacket->size()) + " bytes)";
                        } else {
                            status = "RX guardado pero error al recargar";
                            log.push("[WARN] Error al recargar custom después de guardar RX");
                        }
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
            } else if (ch == 'b' || ch == 'B') {
                showTxBreakdown = !showTxBreakdown;
                status = showTxBreakdown ? "Mostrando desglose TX" : "Mostrando desglose RX";
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

    if (txPanelWin) {
        delwin(txPanelWin);
    }
    if (rxPanelWin) {
        delwin(rxPanelWin);
    }
    if (breakdownWin) {
        delwin(breakdownWin);
    }
    delwin(footerWin);
    delwin(logWin);
    delwin(headerWin);
    endwin();
    return 0;
}
