#include <SFML/Graphics.hpp>
#include <iostream>
#include "Simulation.h"
#include "Renderer.h"

int main()
{
    std::cout << " *** lobPractice LOADING ***" << std::endl;
    std::cout << "DEV Version 20260314 by Konstantin Kalinchenko" << std::endl;
    std::cout << "KostyaEx LOB Simulator — XYZ Stock" << std::endl;

    sf::RenderWindow window(sf::VideoMode({1920, 1200}), "KostyaEx - LOB Simulator");
    window.setFramerateLimit(60);

    Simulation sim;

    Renderer renderer(window);
    if (!renderer.loadAssets()) {
        std::cerr << "WARNING: Could not load font. Text may not render." << std::endl;
    }

    while (window.isOpen())
    {
        while (const std::optional event = window.pollEvent())
        {
            if (event->is<sf::Event::Closed>() ||
                sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Escape))
                window.close();
        }

        sim.tick();
        renderer.render(sim);
    }

    return 0;
}
