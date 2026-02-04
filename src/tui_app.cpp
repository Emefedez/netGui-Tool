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
    int h, w;
    getmaxyx(win, h, w);
    (void)h;
    werase(win);
    box(win, 0, 0);
    const int innerWidth = std::max(0, w - 2);
    const int x = 2;
    const int maxWidth = std::max(0, w - x - 1);

    const std::string line1 = "NetGui-Tool (TUI) [RX:Verde TX:Rojo Warn:Amarillo]";
    const std::string line2 = "Interfaz: " + iface + " | Estado: " + status;

    if (innerWidth > 0) {
        mvwaddnstr(win, 1, x, line1.c_str(), maxWidth);
        mvwaddnstr(win, 2, x, line2.c_str(), maxWidth);
    }
    wrefresh(win);
}

void drawFooter(WINDOW* win) {
    int h, w;
    getmaxyx(win, h, w);
    (void)h;
    werase(win);
    box(win, 0, 0);
    const int x = 2;
    int maxWidth = std::max(0, w - x - 1);
    
    // Modern simplified menu
    if (maxWidth > 0) {
        wattron(win, COLOR_PAIR(6)); // Highlight for groupings
        mvwaddnstr(win, 1, x, "SEND:", 5);
        wattroff(win, COLOR_PAIR(6));
        
        std::string line1 = " [m]Menu";
        mvwaddnstr(win, 1, x + 5, line1.c_str(), maxWidth - 5);

        wattron(win, COLOR_PAIR(6));
        mvwaddnstr(win, 1, x + 35, "EDIT:", 5);
        wattroff(win, COLOR_PAIR(6));
        
        std::string line1b = " [e]Edit [r]Reload [x]SaveRX";
        mvwaddnstr(win, 1, x + 40, line1b.c_str(), maxWidth - 40);

        wattron(win, COLOR_PAIR(6)); // Highlight
        mvwaddnstr(win, 2, x, "SYS:", 4);
        wattroff(win, COLOR_PAIR(6));
        
        std::string line2 = " [t]DemoRX [i]Info [q]Quit [Arrows]Log Scroll";
        mvwaddnstr(win, 2, x + 4, line2.c_str(), maxWidth - 4);
    }
    wrefresh(win);
}

void drawSendMenu(WINDOW* win, bool customLoaded, std::size_t customSize) {
    int h, w;
    getmaxyx(win, h, w);
    (void)h;
    werase(win);
    box(win, 0, 0);

    wattron(win, COLOR_PAIR(6));
    mvwaddnstr(win, 0, 2, " Send Menu ", w - 4);
    wattroff(win, COLOR_PAIR(6));

    mvwaddnstr(win, 1, 2, "[s] Demo A (0x00)", w - 4);
    mvwaddnstr(win, 2, 2, "[d] Demo B (0xFF)", w - 4);
    mvwaddnstr(win, 3, 2, "[c] Enviar Custom", w - 4);
    if (customLoaded) {
        std::string customLine = "Custom: cargado (" + std::to_string(customSize) + " bytes)";
        mvwaddnstr(win, 4, 2, customLine.c_str(), w - 4);
    } else {
        mvwaddnstr(win, 4, 2, "Custom: NO cargado (usa [r] Recargar)", w - 4);
    }
    mvwaddnstr(win, 6, 2, "[m] Cerrar", w - 4);
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

struct WrappedLine {
    std::string text;
    int colorPair = 0;
};

std::vector<std::string> wrapText(const std::string& text, int width) {
    std::vector<std::string> lines;
    if (width <= 0) return lines;

    std::size_t pos = 0;
    while (pos < text.size()) {
        while (pos < text.size() && text[pos] == ' ') {
            ++pos;
        }
        if (pos >= text.size()) break;

        std::size_t lineStart = pos;
        std::size_t lineEnd = pos;
        std::size_t lastSpace = std::string::npos;
        int remaining = width;

        while (lineEnd < text.size() && remaining > 0) {
            if (text[lineEnd] == ' ') {
                lastSpace = lineEnd;
            }
            ++lineEnd;
            --remaining;
        }

        if (lineEnd >= text.size()) {
            lines.push_back(text.substr(lineStart));
            break;
        }

        if (lastSpace != std::string::npos && lastSpace > lineStart) {
            lines.push_back(text.substr(lineStart, lastSpace - lineStart));
            pos = lastSpace + 1;
        } else {
            lines.push_back(text.substr(lineStart, width));
            pos = lineStart + width;
        }
    }

    return lines;
}

std::vector<WrappedLine> buildWrappedLog(const LogBuffer& log, int maxWidth) {
    std::vector<WrappedLine> out;
    for (const auto& line : log.lines) {
        std::string tag;
        int tagColor = 0;
        if (line.rfind("[RX]", 0) == 0) { tag = "[RX]"; tagColor = 1; }
        else if (line.rfind("[TX]", 0) == 0) { tag = "[TX]"; tagColor = 2; }
        else if (line.rfind("[INFO]", 0) == 0) { tag = "[INFO]"; tagColor = 3; }
        else if (line.rfind("[WARN]", 0) == 0) { tag = "[WARN]"; tagColor = 4; }

        int colorPair = (tagColor > 0) ? tagColor : 5;
        std::string body = line;
        std::string prefix;
        if (!tag.empty()) {
            body = line.substr(tag.size());
            if (!body.empty() && body[0] == ' ') body.erase(0, 1);
            prefix = tag + " ";
        }

        const int prefixLen = static_cast<int>(prefix.size());
        const int bodyWidth = std::max(0, maxWidth - prefixLen);
        auto wrapped = wrapText(body, bodyWidth);
        if (wrapped.empty()) wrapped.push_back(" ");

        for (std::size_t i = 0; i < wrapped.size(); ++i) {
            std::string lineText;
            if (!prefix.empty()) {
                if (i == 0) {
                    lineText = prefix + wrapped[i];
                } else {
                    lineText = std::string(prefixLen, ' ') + wrapped[i];
                }
            } else {
                lineText = wrapped[i];
            }
            out.push_back({lineText, colorPair});
        }
    }
    return out;
}

void drawLog(WINDOW* win, const LogBuffer& log, int scrollOffset) {
    werase(win);
    box(win, 0, 0);
    int h, w;
    getmaxyx(win, h, w);
    const int maxLines = h - 2;
    const int maxWidth = std::max(0, w - 4);
    auto wrapped = buildWrappedLog(log, maxWidth);
    int start = 0;
    const int total = static_cast<int>(wrapped.size());
    const int maxStart = (total > maxLines) ? (total - maxLines) : 0;
    if (scrollOffset > maxStart) scrollOffset = maxStart;
    if (scrollOffset < 0) scrollOffset = 0;
    start = maxStart - scrollOffset;
    for (int i = 0; i < maxLines && (start + i) < total; ++i) {
        const auto& line = wrapped[start + i];
        wmove(win, 1 + i, 2);
        if (line.colorPair > 0) wattron(win, COLOR_PAIR(line.colorPair));
        mvwaddnstr(win, 1 + i, 2, line.text.c_str(), maxWidth);
        if (line.colorPair > 0) wattroff(win, COLOR_PAIR(line.colorPair));
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
    wattron(win, COLOR_PAIR(2));
    mvwprintw(win, y++, 2, "Dst: %s", macToString(lastTxFrame->dst).c_str());
    mvwprintw(win, y++, 2, "Src: %s", macToString(lastTxFrame->src).c_str());
    mvwprintw(win, y++, 2, "Tipo: 0x%04X (%s)", lastTxFrame->etherType, etherTypeLabel(lastTxFrame->etherType).c_str());
    wattroff(win, COLOR_PAIR(2));
    y++;
    
    // Payload en hex + ASCII
    mvwprintw(win, y++, 2, "Payload (%zu bytes):", lastTxFrame->payload.size());
    const auto& payload = lastTxFrame->payload;
    // Calculate dynamic bytes per line: Width = 11 + 4*N
    int bytesPerLine = (w - 11) / 4;
    if (bytesPerLine < 4) bytesPerLine = 4;
    if (bytesPerLine > 16) bytesPerLine = 16;

    const int maxLines = h - y - 2; // Espacio disponible
    
    for (std::size_t i = 0; i < payload.size() && (y - 6) < maxLines; i += bytesPerLine) {
        // Offset
        wattron(win, COLOR_PAIR(2));
        mvwprintw(win, y, 2, "%04zX", i);
        wattroff(win, COLOR_PAIR(2));
        
        // Hex bytes
        int x = 7;
        for (std::size_t j = 0; j < static_cast<std::size_t>(bytesPerLine) && (i + j) < payload.size(); ++j) {
            wattron(win, COLOR_PAIR(2));
            mvwprintw(win, y, x, "%02X", payload[i + j]);
            wattroff(win, COLOR_PAIR(2));
            x += 3;
        }
        
        // Parte ASCII
        x = 7 + bytesPerLine * 3 + 2;
        if (x + bytesPerLine < w - 2) {
            wattron(win, COLOR_PAIR(3));
            for (std::size_t j = 0; j < static_cast<std::size_t>(bytesPerLine) && (i + j) < payload.size(); ++j) {
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
    wattron(win, COLOR_PAIR(1));
    mvwprintw(win, y++, 2, "Dst: %s", macToString(lastRxFrame->dst).c_str());
    mvwprintw(win, y++, 2, "Src: %s", macToString(lastRxFrame->src).c_str());
    mvwprintw(win, y++, 2, "Tipo: 0x%04X (%s)", lastRxFrame->etherType, etherTypeLabel(lastRxFrame->etherType).c_str());
    wattroff(win, COLOR_PAIR(1));
    y++;
    
    // Payload en hex + ASCII
    mvwprintw(win, y++, 2, "Payload (%zu bytes):", lastRxFrame->payload.size());
    const auto& payload = lastRxFrame->payload;
    // Calculate dynamic bytes per line: Width = 11 + 4*N
    int bytesPerLine = (w - 11) / 4;
    if (bytesPerLine < 4) bytesPerLine = 4;
    if (bytesPerLine > 16) bytesPerLine = 16;
    
    const int maxLines = h - y - 2; // Espacio disponible
    
    for (std::size_t i = 0; i < payload.size() && (y - 6) < maxLines; i += bytesPerLine) {
        // Offset
        wattron(win, COLOR_PAIR(1));
        mvwprintw(win, y, 2, "%04zX", i);
        wattroff(win, COLOR_PAIR(1));
        
        // Hex bytes
        int x = 7;
        for (std::size_t j = 0; j < static_cast<std::size_t>(bytesPerLine) && (i + j) < payload.size(); ++j) {
            wattron(win, COLOR_PAIR(1));
            mvwprintw(win, y, x, "%02X", payload[i + j]);
            wattroff(win, COLOR_PAIR(1));
            x += 3;
        }
        
        // Parte ASCII
        x = 7 + bytesPerLine * 3 + 2;
        if (x + bytesPerLine < w - 2) {
            wattron(win, COLOR_PAIR(3));
            for (std::size_t j = 0; j < static_cast<std::size_t>(bytesPerLine) && (i + j) < payload.size(); ++j) {
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
    
    // Línea compacta de MAC Destino
    std::string line = "Dst: ";
    wattron(win, COLOR_PAIR(4));
    for (int i = 0; i < 6 && i < (int)frameBytes.size(); ++i) {
        char buf[4];
        snprintf(buf, sizeof(buf), "%02X ", frameBytes[i]);
        line += buf;
    }
    wattroff(win, COLOR_PAIR(4));
    if (line.size() + 20 < (size_t)w) {
        line += " | ";
        line += macToString(lastFrame->dst);
    }
    mvwprintw(win, y++, 2, "%s", line.c_str());
    
    // Línea compacta de MAC Origen
    line = "Src: ";
    wattron(win, COLOR_PAIR(3));
    for (int i = 6; i < 12 && i < (int)frameBytes.size(); ++i) {
        char buf[4];
        snprintf(buf, sizeof(buf), "%02X ", frameBytes[i]);
        line += buf;
    }
    wattroff(win, COLOR_PAIR(3));
    if (line.size() + 20 < (size_t)w) {
        line += " | ";
        line += macToString(lastFrame->src);
    }
    mvwprintw(win, y++, 2, "%s", line.c_str());
    
    // Línea de EtherType
    line = "Type: ";
    init_pair(5, COLOR_MAGENTA, COLOR_BLACK);
    wattron(win, COLOR_PAIR(5));
    if (12 < (int)frameBytes.size() && 13 < (int)frameBytes.size()) {
        char buf[20];
        snprintf(buf, sizeof(buf), "%02X %02X", frameBytes[12], frameBytes[13]);
        line += buf;
    }
    wattroff(win, COLOR_PAIR(5));
    line += " | ";
    char etBuf[40];
    snprintf(etBuf, sizeof(etBuf), "0x%04X (%s)", lastFrame->etherType, etherTypeLabel(lastFrame->etherType).c_str());
    line += etBuf;
    mvwprintw(win, y++, 2, "%s", line.c_str());
    
    // Línea de Payload
    line = "Payload: ";
    wattron(win, COLOR_PAIR(1));
    int bytesShown = 0;
    for (std::size_t i = 0; i < lastFrame->payload.size() && bytesShown < 16; ++i, ++bytesShown) {
        char buf[4];
        snprintf(buf, sizeof(buf), "%02X ", lastFrame->payload[i]);
        line += buf;
    }
    wattroff(win, COLOR_PAIR(1));
    if (lastFrame->payload.size() > 16) {
        line += "...";
    }
    mvwprintw(win, y++, 2, "%s", line.c_str());
    
    // Línea de resumen
    if (y < h - 1) {
        char sumBuf[80];
        snprintf(sumBuf, sizeof(sumBuf), "Total: %zu bytes (14 header + %zu payload)", frameBytes.size(), lastFrame->payload.size());
        mvwprintw(win, y++, 2, "%s", sumBuf);
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
        mvwprintw(win, 14, 2, "Trama Ethernet: MAC dst (6B) + MAC src (6B) + EtherType (2B) + Payload");
        // mvwprintw(win, 16, 2, "EtherType: 0x0800=IPv4, 0x0806=ARP, 0x86DD=IPv6, 0x88B5=Demo.");
        
        mvwprintw(win, 16, 2, "Ejemplos de prueba (Terminal):");
        wattron(win, COLOR_PAIR(4));
        mvwprintw(win, 17, 2, "  ping -I tap_user 8.8.8.8   (Genera RX)");
        mvwprintw(win, 18, 2, "  tcpdump -i tap_user -n     (Verifica TX/RX)");
        wattroff(win, COLOR_PAIR(4));

        mvwprintw(win, 20, 2, "Controles: [i] Info  [-] Pagina anterior  [+] Siguiente  [q] Salir");
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
        init_pair(3, COLOR_CYAN, COLOR_BLACK);  // INFO/ASCII
        init_pair(4, COLOR_YELLOW, COLOR_BLACK);// WARN/Labels
        init_pair(5, COLOR_WHITE, COLOR_BLACK); // Normal
        init_pair(6, COLOR_BLUE, COLOR_BLACK);  // Offsets/Menu
    }

    int termH, termW;
    getmaxyx(stdscr, termH, termW);
    int headerH = 4;
    int footerH = 4;
    
    // No top breakdown panel
    int breakdownH = 0;
    
    int logH = termH - headerH - footerH - breakdownH;
    if (logH < 4) logH = 4;

    // Crear paneles laterales para TX/RX
    // Ahora son más grandes al no haber breakdown panel
    int sidePanelW = 0;
    int logW = termW;
    WINDOW* txPanelWin = nullptr;
    WINDOW* rxPanelWin = nullptr;
    
    if (termW > 140) {  // Espacio para ambos paneles
        sidePanelW = (int)(termW * 0.30); 
        if (sidePanelW < 40) sidePanelW = 40;
        
        logW = termW - (sidePanelW * 2);
        txPanelWin = newwin(logH, sidePanelW, headerH + breakdownH, 0);
        rxPanelWin = newwin(logH, sidePanelW, headerH + breakdownH, termW - sidePanelW);
    } else if (termW > 90) {  // Espacio solo para panel RX (prioridad sniffing)
        sidePanelW = (int)(termW * 0.40);
        
        logW = termW - sidePanelW;
        rxPanelWin = newwin(logH, sidePanelW, headerH + breakdownH, logW);
    }
    // else: solo log

    WINDOW* headerWin = newwin(headerH, termW, 0, 0);
    // breakdownWin removed
    int logX = (txPanelWin != nullptr) ? sidePanelW : 0;
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
    bool showSendMenu = false;
    while (running) {
        if (showInfo) {
            // Fullscreen info to avoid flicker from other panels
            drawInfo(stdscr, infoPage);
        } else {
            drawHeader(headerWin, tap.name(), status);
            drawLog(logWin, log, scrollOffset);
            if (txPanelWin) {
                drawLastTxPanel(txPanelWin, lastTxFrame);
            }
            if (rxPanelWin) {
                drawLastRxPanel(rxPanelWin, lastRxFrame);
            }
            drawFooter(footerWin);

            if (showSendMenu) {
                int const popupH = 7;
                int const popupW = 32;
                int const popupY = (termH - popupH) / 2;
                int const popupX = (termW - popupW) / 2;
                WINDOW* sendMenuWin = newwin(popupH, popupW, popupY, popupX);
                drawSendMenu(sendMenuWin, customPacket.has_value(), customPacket ? customPacket->size() : 0);
                delwin(sendMenuWin);
            }
        }

        struct pollfd pfd;
        pfd.fd = tap.getFd();
        pfd.events = POLLIN;
        int ret = poll(&pfd, 1, 10);  // 10ms para mejor responsividad de teclado

        int ch = getch();
        if (ch != ERR) {
            if (ch == 'q' || ch == 'Q') {
                status = "Saliendo...";
                running = false;
            } else if (ch == 'i' || ch == 'I') {
                showInfo = !showInfo;
                if (showInfo) showSendMenu = false;
                infoPage = 0;
            } else if (ch == '-' && showInfo) {
                infoPage = (infoPage - 1 + 3) % 3;
            } else if (ch == '+' && showInfo) {
                infoPage = (infoPage + 1) % 3;
            } else if (ch == 'm' || ch == 'M') {
                if (!showInfo) {
                    showSendMenu = !showSendMenu;
                }
            } else if ((ch == 's' || ch == 'S') && showSendMenu) {
                auto frame = makeDefaultDemoFrame(0);
                lastTxFrame = frame;
                auto bytes = serializeEthernetII(frame);
                int sent = tap.write(bytes.data(), bytes.size());
                status = txResult(sent);
                log.push("[TX] Demo 0x00 (" + std::to_string(bytes.size()) + "B) -> " + status);
                showSendMenu = false;
            } else if ((ch == 'd' || ch == 'D') && showSendMenu) {
                auto frame = makeDefaultDemoFrame(1);
                lastTxFrame = frame;
                auto bytes = serializeEthernetII(frame);
                int sent = tap.write(bytes.data(), bytes.size());
                status = txResult(sent);
                log.push("[TX] Demo 0xFF (" + std::to_string(bytes.size()) + "B) -> " + status);
                showSendMenu = false;
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
                    init_pair(5, COLOR_WHITE, COLOR_BLACK);
                    init_pair(6, COLOR_BLUE, COLOR_BLACK);
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
            } else if ((ch == 'c' || ch == 'C') && showSendMenu) {
                if (!customPacket) {
                    status = "Custom no cargado";
                    log.push("[WARN] [TX] Custom falló: no hay bytes");
                } else {
                    // Parsear el custom para guardarlo en lastTxFrame
                    auto customFrameOpt = parseEthernetII(customPacket->data(), customPacket->size());
                    if (customFrameOpt) {
                        lastTxFrame = customFrameOpt;
                    }
                    int sent = tap.write(customPacket->data(), customPacket->size());
                    status = txResult(sent);
                    log.push("[TX] Custom -> " + status);
                }
                showSendMenu = false;
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
            } else if (ch == KEY_UP) {
                scrollOffset += 1; // Scroll up (back in history)
            } else if (ch == KEY_DOWN) {
                scrollOffset -= 1; // Scroll down (forward to recent)
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
    // if (breakdownWin) delwin(breakdownWin); REMOVED
    delwin(footerWin);
    delwin(logWin);
    delwin(headerWin);
    endwin();
    return 0;
}
