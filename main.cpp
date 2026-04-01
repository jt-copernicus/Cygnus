/*
 * cygnus-paint
 * Paint program for Cygnus WM.
 *
 * by Jonathan Torres
 * 
 * This program is free software: you can redistribute it and/or modify it under the terms of the 
 * GNU General Public License as published by the Free Software Foundation, either version 3 of 
 * the License, or (at your option) any later version.
 * 
 */

#include <SFML/Graphics.hpp>
#include <vector>
#include <string>
#include <queue>
#include <iostream>
#include <cmath>
#include <algorithm>
#include <cstdlib>

const sf::Color COL_BG(0x1a, 0x1a, 0x1a);
const sf::Color COL_FG(0xff, 0xff, 0xff);
const sf::Color COL_SEL(0x33, 0x66, 0xff);
const sf::Color COL_TOPBAR(0x22, 0x22, 0x22);
const sf::Color COL_BORDER(0x44, 0x44, 0x44);

struct PaletteColor {
    std::string name;
    sf::Color color;
};

const std::vector<PaletteColor> PALETTE = {
    {"Black", sf::Color(0, 0, 0)},
    {"White", sf::Color(255, 255, 255)},
    {"Gray", sf::Color(128, 128, 128)},
    {"Dark Gray", sf::Color(64, 64, 64)},
    {"Red", sf::Color(255, 0, 0)},
    {"Orange", sf::Color(255, 165, 0)},
    {"Light Yellow", sf::Color(255, 255, 153)},
    {"Dark Yellow", sf::Color(204, 204, 0)},
    {"Light Blue", sf::Color(173, 216, 230)},
    {"Dark Blue", sf::Color(0, 0, 139)},
    {"Light Green", sf::Color(144, 238, 144)},
    {"Dark Green", sf::Color(0, 100, 0)},
    {"Light Pink", sf::Color(255, 182, 193)},
    {"Dark Pink", sf::Color(255, 20, 147)},
    {"Purple", sf::Color(128, 0, 128)},
    {"Light Brown", sf::Color(181, 101, 29)},
    {"Dark Brown", sf::Color(101, 67, 33)}
};

enum class Tool { Brush, Bucket };

class Canvas {
public:
    sf::Image image;
    sf::Texture texture;
    sf::Sprite sprite;
    std::vector<sf::Image> undoStack;
    std::vector<sf::Image> redoStack;
    const size_t maxHistory = 20;
    bool dirty = false;

    Canvas(unsigned int width, unsigned int height) {
        image.create(width, height, sf::Color::White);
        updateTexture();
        saveState();
    }

    void updateTexture() {
        texture.loadFromImage(image);
        sprite.setTexture(texture, true);
    }

    void saveState() {
        undoStack.push_back(image);
        if (undoStack.size() > maxHistory) {
            undoStack.erase(undoStack.begin());
        }
        redoStack.clear();
    }

    void undo() {
        if (undoStack.size() > 1) {
            redoStack.push_back(undoStack.back());
            undoStack.pop_back();
            image = undoStack.back();
            updateTexture();
        }
    }

    void redo() {
        if (!redoStack.empty()) {
            image = redoStack.back();
            undoStack.push_back(redoStack.back());
            redoStack.pop_back();
            updateTexture();
        }
    }

    void drawBrush(sf::Vector2i p1, sf::Vector2i p2, int radius, sf::Color color) {
        float distance = std::sqrt(std::pow(p2.x - p1.x, 2) + std::pow(p2.y - p1.y, 2));
        int steps = std::max(static_cast<int>(distance), 1);
        for (int i = 0; i <= steps; ++i) {
            float t = static_cast<float>(i) / steps;
            int x = static_cast<int>(p1.x + (p2.x - p1.x) * t);
            int y = static_cast<int>(p1.y + (p2.y - p1.y) * t);
            drawCircle(x, y, radius, color);
        }
    }

    void drawCircle(int cx, int cy, int radius, sf::Color color) {
        for (int y = -radius; y <= radius; ++y) {
            for (int x = -radius; x <= radius; ++x) {
                if (x*x + y*y <= radius*radius) {
                    int px = cx + x;
                    int py = cy + y;
                    if (px >= 0 && px < static_cast<int>(image.getSize().x) &&
                        py >= 0 && py < static_cast<int>(image.getSize().y)) {
                        image.setPixel(px, py, color);
                    }
                }
            }
        }
    }

    void floodFill(int x, int y, sf::Color targetColor) {
        sf::Color oldColor = image.getPixel(x, y);
        if (oldColor == targetColor) return;

        std::queue<sf::Vector2i> q;
        q.push({x, y});
        image.setPixel(x, y, targetColor);

        while (!q.empty()) {
            sf::Vector2i curr = q.front();
            q.pop();

            sf::Vector2i neighbors[] = {{0,1}, {0,-1}, {1,0}, {-1,0}};
            for (auto& n : neighbors) {
                int nx = curr.x + n.x;
                int ny = curr.y + n.y;
                if (nx >= 0 && nx < static_cast<int>(image.getSize().x) &&
                    ny >= 0 && ny < static_cast<int>(image.getSize().y) &&
                    image.getPixel(nx, ny) == oldColor) {
                    image.setPixel(nx, ny, targetColor);
                    q.push({nx, ny});
                }
            }
        }
    }
};

int main() {
    const int windowWidth = 800;
    const int windowHeight = 600;
    const int toolbarHeight = 100;

    sf::RenderWindow window(sf::VideoMode(windowWidth, windowHeight), "CPaint", sf::Style::None);
    window.setFramerateLimit(60);

    Canvas canvas(windowWidth, windowHeight - toolbarHeight);
    
    sf::View canvasView(sf::FloatRect(0, 0, windowWidth, windowHeight - toolbarHeight));
    canvasView.setViewport(sf::FloatRect(0, 0, 1.0f, static_cast<float>(windowHeight - toolbarHeight) / windowHeight));

    sf::View uiView(sf::FloatRect(0, 0, windowWidth, toolbarHeight));
    uiView.setViewport(sf::FloatRect(0, static_cast<float>(windowHeight - toolbarHeight) / windowHeight, 1.0f, static_cast<float>(toolbarHeight) / windowHeight));

    sf::Color currentColor = sf::Color::Black;
    Tool currentTool = Tool::Brush;
    int brushSize = 5;
    float zoomLevel = 1.0f;
    bool isDrawing = false;
    sf::Vector2i lastMousePos;

    sf::Font font;
    std::vector<std::string> fontPaths = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/freefont/FreeSans.ttf",
        "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf"
    };
    bool fontLoaded = false;
    for (const auto& path : fontPaths) {
        if (font.loadFromFile(path)) {
            fontLoaded = true;
            break;
        }
    }
    if (!fontLoaded) {
        std::cerr << "Could not load font. UI text will be missing." << std::endl;
    }

    struct ButtonDef {
        std::string label;
        float width;
    };
    std::vector<ButtonDef> uiButtons = {
        {"Brush", 80}, {"Bucket", 80}, {"-", 30}, {"+", 30}, 
        {"Undo", 60}, {"Redo", 60}, {"Z In", 60}, {"Z Out", 60}, 
        {"Save", 60}
    };

    auto getButtonRects = [&](float winWidth, float yPos, float height) {
        std::vector<sf::FloatRect> rects;
        float totalBtnW = 0;
        for (const auto& b : uiButtons) totalBtnW += b.width;
        float availableSpace = winWidth - 20;
        float gap = std::max(5.0f, (availableSpace - totalBtnW) / (uiButtons.size() - 1));
        float x = 10;
        for (const auto& b : uiButtons) {
            rects.push_back(sf::FloatRect(x, yPos, b.width, height));
            x += b.width + gap;
        }
        return rects;
    };

    while (window.isOpen()) {
        sf::Event event;
        while (window.pollEvent(event)) {
            if (event.type == sf::Event::Closed)
                window.close();
            
            if (event.type == sf::Event::Resized) {
                float w = static_cast<float>(event.size.width);
                float h = static_cast<float>(event.size.height);
                canvasView.setSize(w, h - toolbarHeight);
                canvasView.setViewport(sf::FloatRect(0, 0, 1.0f, (h - toolbarHeight) / h));
                uiView.setSize(w, toolbarHeight);
                uiView.setViewport(sf::FloatRect(0, (h - toolbarHeight) / h, 1.0f, toolbarHeight / h));
            }

            if (event.type == sf::Event::MouseButtonPressed) {
                if (event.mouseButton.button == sf::Mouse::Left) {
                    sf::Vector2i mousePos = sf::Mouse::getPosition(window);
                    
                    if (mousePos.y < static_cast<int>(window.getSize().y) - toolbarHeight) {
                        sf::Vector2f canvasPos = window.mapPixelToCoords(mousePos, canvasView);
                        sf::Vector2i p(static_cast<int>(canvasPos.x), static_cast<int>(canvasPos.y));

                        if (currentTool == Tool::Brush) {
                            isDrawing = true;
                            lastMousePos = p;
                            canvas.drawCircle(p.x, p.y, brushSize, currentColor);
                            canvas.dirty = true;
                        } else if (currentTool == Tool::Bucket) {
                            if (p.x >= 0 && p.x < static_cast<int>(canvas.image.getSize().x) &&
                                p.y >= 0 && p.y < static_cast<int>(canvas.image.getSize().y)) {
                                canvas.saveState();
                                canvas.floodFill(p.x, p.y, currentColor);
                                canvas.dirty = true;
                            }
                        }
                    } else {
                        sf::Vector2f uiPos = window.mapPixelToCoords(mousePos, uiView);
                        for (size_t i = 0; i < PALETTE.size(); ++i) {
                            sf::FloatRect colorRect(10 + i * 30, 10, 25, 25);
                            if (colorRect.contains(uiPos)) {
                                currentColor = PALETTE[i].color;
                            }
                        }
                        float totalBtnW = 0;
                        for (const auto& b : uiButtons) totalBtnW += b.width;
                        float availWidth = window.getSize().x - 20;
                        float gap = (availWidth - totalBtnW) / (uiButtons.size() - 1);
                        if (gap < 5) gap = 5;

                        float currentX = 10;
                        for (size_t i = 0; i < uiButtons.size(); ++i) {
                            sf::FloatRect btnRect(currentX, 50, uiButtons[i].width, 25);
                            if (btnRect.contains(uiPos)) {
                                switch (i) {
                                    case 0: currentTool = Tool::Brush; break;
                                    case 1: currentTool = Tool::Bucket; break;
                                    case 2: brushSize = std::max(1, brushSize - 1); break;
                                    case 3: brushSize = std::min(50, brushSize + 1); break;
                                    case 4: canvas.undo(); break;
                                    case 5: canvas.redo(); break;
                                    case 6: zoomLevel *= 0.9f; canvasView.zoom(0.9f); break;
                                    case 7: zoomLevel *= 1.1f; canvasView.zoom(1.1f); break;
                                    case 8: {
                                        const char* home = std::getenv("HOME");
                                        std::string path = (home ? std::string(home) + "/Pictures/cpaint_drawing.png" : "cpaint_drawing.png");
                                        canvas.image.saveToFile(path);
                                    } break;
                                }
                            }
                            currentX += uiButtons[i].width + gap;
                        }
                    }
                }
            }

            if (event.type == sf::Event::MouseButtonReleased) {
                if (event.mouseButton.button == sf::Mouse::Left && isDrawing) {
                    isDrawing = false;
                    canvas.saveState();
                    canvas.updateTexture();
                }
            }

            if (event.type == sf::Event::MouseMoved && isDrawing) {
                sf::Vector2i mousePos = sf::Mouse::getPosition(window);
                sf::Vector2f canvasPos = window.mapPixelToCoords(mousePos, canvasView);
                sf::Vector2i p(static_cast<int>(canvasPos.x), static_cast<int>(canvasPos.y));
                canvas.drawBrush(lastMousePos, p, brushSize, currentColor);
                lastMousePos = p;
                canvas.dirty = true; 
            }
        }

        if (canvas.dirty) {
            canvas.updateTexture();
            canvas.dirty = false;
        }

        window.clear(COL_BG);
        window.setView(canvasView);
        window.draw(canvas.sprite);
        window.setView(uiView);
        sf::RectangleShape toolbarBg(sf::Vector2f(window.getSize().x, toolbarHeight));
        toolbarBg.setFillColor(COL_TOPBAR);
        window.draw(toolbarBg);
        sf::RectangleShape topBorder(sf::Vector2f(window.getSize().x, 2));
        topBorder.setFillColor(COL_BORDER);
        topBorder.setPosition(0, 0);
        window.draw(topBorder);


        for (size_t i = 0; i < PALETTE.size(); ++i) {
            sf::RectangleShape colorBox(sf::Vector2f(25, 25));
            colorBox.setPosition(10 + i * 30, 10);
            colorBox.setFillColor(PALETTE[i].color);
            colorBox.setOutlineThickness(1);
            if (PALETTE[i].color == currentColor) {
                colorBox.setOutlineThickness(2);
                colorBox.setOutlineColor(COL_SEL);
            } else {
                colorBox.setOutlineColor(COL_BORDER);
            }
            window.draw(colorBox);
        }

        auto drawButton = [&](float x, float y, float w, float h, std::string label, bool active = false) {
            sf::RectangleShape btn(sf::Vector2f(w, h));
            btn.setPosition(x, y);
            btn.setFillColor(active ? COL_SEL : COL_TOPBAR);
            btn.setOutlineThickness(1);
            btn.setOutlineColor(COL_BORDER);
            window.draw(btn);
            if (fontLoaded) {
                sf::Text text(label, font, 12);
                text.setFillColor(COL_FG);
                sf::FloatRect textRect = text.getLocalBounds();
                text.setOrigin(textRect.left + textRect.width/2.0f, textRect.top  + textRect.height/2.0f);
                text.setPosition(x + w/2.0f, y + h/2.0f);
                window.draw(text);
            }
        };

        auto btnRects = getButtonRects(window.getSize().x, 50, 25);
        for(size_t i = 0; i < uiButtons.size(); ++i) {
            bool active = (i == 0 && currentTool == Tool::Brush) || (i == 1 && currentTool == Tool::Bucket);
            drawButton(btnRects[i].left, btnRects[i].top, btnRects[i].width, btnRects[i].height, uiButtons[i].label, active);
        }

        if (fontLoaded) {
            sf::Text sizeText("Size: " + std::to_string(brushSize), font, 12);
            sizeText.setFillColor(COL_FG);
            sizeText.setPosition(10, 80);
            window.draw(sizeText);

            sf::Text toolText("Tool: " + std::string(currentTool == Tool::Brush ? "Brush" : "Bucket"), font, 12);
            toolText.setFillColor(COL_FG);
            toolText.setPosition(100, 80);
            window.draw(toolText);

            sf::Text zoomText("Zoom: " + std::to_string(static_cast<int>(zoomLevel * 100)) + "%", font, 12);
            zoomText.setFillColor(COL_FG);
            zoomText.setPosition(220, 80);
            window.draw(zoomText);
        }

        window.setView(window.getDefaultView());
        sf::RectangleShape winBorder(sf::Vector2f(window.getSize().x, window.getSize().y));
        winBorder.setFillColor(sf::Color::Transparent);
        winBorder.setOutlineThickness(-2);
        winBorder.setOutlineColor(COL_BORDER);
        window.draw(winBorder);

        window.display();
    }

    return 0;
}
