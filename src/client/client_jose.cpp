#include "api.h"
#include "utils.h"
#include <iostream>
#include <random>
#include <spdlog/spdlog.h>
#include <string>
#include <tuple>
#include <utility>
#include <queue>
#include <set>
using namespace cycles;



class BotClient {
    Connection connection;
    std::string name;
    GameState state;
    Player my_player;
    std::mt19937 rng;
    int previousDirection = -1;
    int inertia = 30;

    bool is_valid_move(Direction direction) {
        // Check that the move does not overlap with any grid cell that is set to
        // not 0
        auto new_pos = my_player.position + getDirectionVector(direction);
        if (!state.isInsideGrid(new_pos)) {
            return false;
        }
        if (state.getGridCell(new_pos) != 0) {
            return false;
        }

        return true;
    }


    std::pair<sf::Vector2i, Player> findNearestOpponentHead() {
        int minDistance = INT_MAX;
        sf::Vector2i nearestOpponentPosition = {-1, -1};
        Player nearestOpponent;

        for (const auto& player : state.players) {
            // Skip if this is the bot itself or if the player is not active
            if (player.name == name) continue;

            // Calculate Manhattan distance to the opponent's head
            int distance = abs(my_player.position.x - player.position.x) +
                        abs(my_player.position.y - player.position.y);

            // Update nearest opponent if this one is closer
            if (distance < minDistance) {
                minDistance = distance;
                nearestOpponentPosition = player.position;
                nearestOpponent = player;
            }
        }

        // spdlog::info("Nearest opponent's position: {}, Nearest Player: {}", nearestOpponentPosition, nearestOpponent);

        // Return the position and the opponent's `Player` object
        return std::make_pair(nearestOpponentPosition, nearestOpponent);
    }

    sf::Vector2i predictOpponentMove(const sf::Vector2i& opponentHead) {
         sf::Vector2i nextHead = opponentHead;

        // Check all directions and predict the opponent's move
        for (auto d : {Direction::north, Direction::east, Direction::south, Direction::west}) {
            sf::Vector2i candidateMove = opponentHead + getDirectionVector(d);

            // Predict opponent's move if it's valid
            if (state.isInsideGrid(candidateMove) && state.getGridCell(candidateMove) == 0) {
                nextHead = candidateMove;
                break;
            }
        }


        return nextHead;
    }


    Direction approachTarget(const sf::Vector2i& target) {
        std::vector<std::pair<Direction, int>> moves;

        // Evaluate each possible direction based on proximity to the target
        for (auto d : {Direction::north, Direction::south, Direction::east, Direction::west}) {
            sf::Vector2i new_pos = my_player.position + getDirectionVector(d);

            if (is_valid_move(d)) {
                int distance = abs(new_pos.x - target.x) + abs(new_pos.y - target.y);
                moves.emplace_back(d, distance);
            }
        }

        // Sort moves by proximity (smallest distance first)
        std::sort(moves.begin(), moves.end(), [](const auto& a, const auto& b) {
            return a.second < b.second;
        });

        return moves.empty() ? Direction::north : moves[0].first; // Default fallback
    }

    struct Vector2Comparator {
        bool operator()(const sf::Vector2i& a, const sf::Vector2i& b) const {
            if (a.x != b.x) {
                return a.x < b.x; // Compare by x-coordinate first
            }
            return a.y < b.y; // Compare by y-coordinate if x is equal
        }
    };

    int calculateAccessibleArea(const sf::Vector2i& start) {
        std::queue<sf::Vector2i> toVisit;
        std::set<sf::Vector2i, Vector2Comparator> visited; // Use the custom comparator
        toVisit.push(start);
        int area = 0;

        // Define the possible moves explicitly
        std::array<sf::Vector2i, 4> deltas = {
            sf::Vector2i{0, -1}, // Up
            sf::Vector2i{0, 1},  // Down
            sf::Vector2i{-1, 0}, // Left
            sf::Vector2i{1, 0}   // Right
        };

        while (!toVisit.empty()) {
            sf::Vector2i current = toVisit.front();
            toVisit.pop();

            // Skip if this cell is out of bounds, occupied, or already visited
            if (visited.count(current) || !state.isInsideGrid(current) || state.getGridCell(current) != 0) {
                continue;
            }

            visited.insert(current); // Mark as visited
            area++;

            // Add neighbors to the queue
            for (const auto& delta : deltas) {
                sf::Vector2i neighbor = current + delta;
                toVisit.push(neighbor);
            }
        }

        return area;
    }

    Direction findSafeDirection() {
        std::vector<std::pair<Direction, int>> moves;

        // Evaluate each possible direction
        for (auto dir : {Direction::north, Direction::east, Direction::south, Direction::west}) {
            if (is_valid_move(dir)) {

                // Calculate the accessible area if moving in this direction
                auto new_pos = my_player.position + getDirectionVector(dir);
                int accessibleArea = calculateAccessibleArea(new_pos);
                moves.emplace_back(dir, accessibleArea);
            }
        }

        // Sort moves by accessible area (largest area first)
        std::sort(moves.begin(), moves.end(), [](const auto& a, const auto& b) {
            return a.second > b.second;
        });

        // Return the direction leading to the largest area or a default direction
        return moves.empty() ? Direction::north : moves[0].first;
    }

    Direction fallBackMove() {
        spdlog::info("{} is making a fallback after getting too close", name);

        Direction safeDirection = findSafeDirection();

        // If the safe direction is the same as the previous direction, continue
        if (safeDirection == static_cast<Direction>(previousDirection)) {
            return safeDirection;
        }

        // Otherwise, turn to the safe direction
        previousDirection = static_cast<int>(safeDirection); // Update for future use
        return safeDirection;
        }

    Direction decideMove() {
        constexpr int max_attempts = 200;
        int attempts = 0;
        const auto position = my_player.position;
        const int frameNumber = state.frameNumber;

        /*  Strategy for the robot

            The idea is for the robot to be really aggressive, it should look for the nearest bot,
            figure out where it is moving and where its head is, and then try to surround it.
            If while surrounding it, the bot sees the next move will collapse, it should move away (this is what the fallback does)
        */
        Direction direction;

        // Find the nearest opponent
        auto [nearestHead, nearestOpponent] = findNearestOpponentHead();

        // If no opponents are alive or reachable, fallback
        if (nearestHead == sf::Vector2i{-1, -1}) {
            return fallBackMove();
        }

        // Predict the opponent’s next move
        sf::Vector2i predictedPosition = predictOpponentMove(nearestHead);

        // Determine the best direction to move closer to the predicted position
        direction = approachTarget(predictedPosition);


        while (!is_valid_move(direction)) {
            if (attempts >= max_attempts) {
                spdlog::error("{}: Failed to find a valid move after {} attempts", name, max_attempts);
                exit(1);
            }

            // basically, if the move is not valid
            direction = fallBackMove();
            attempts++;
        }


       spdlog::debug("{}: Aggressively targeting at ({}, {}), predicted move ({}, {}), moving from ({}, {}) to ({}, {}) in frame {}",
                  name, nearestHead.x, nearestHead.y,
                  predictedPosition.x, predictedPosition.y,
                  position.x, position.y,
                  position.x + getDirectionVector(direction).x,
                  position.y + getDirectionVector(direction).y, frameNumber);

        return direction;
    }

    void receiveGameState() {
        state = connection.receiveGameState();
        for (const auto &player : state.players) {
            if (player.name == name) {
                my_player = player;
                break;
            }
        }
    }

    void sendMove() {
        spdlog::debug("{}: Sending move", name);
        auto move = decideMove();
        previousDirection = getDirectionValue(move);
        connection.sendMove(move);
    }

    public:
        BotClient(const std::string &botName) : name(botName) {
            std::random_device rd;
            rng.seed(rd());
            std::uniform_int_distribution<int> dist(0, 50);
            inertia = dist(rng);
            connection.connect(name);

            if (!connection.isActive()) {
                spdlog::critical("{}: Connection failed", name);
                exit(1);
            }
        }

        void run() {
            while (connection.isActive()) {
                receiveGameState();
                sendMove();
            }
        }
};


int main(int argc, char *argv[]) {
  if (argc != 2) {
    std::cerr << "Usage: " << argv[0] << " <bot_name>" << std::endl;
    return 1;
  }

    #if SPDLOG_ACTIVE_LEVEL == SPDLOG_LEVEL_TRACE
    spdlog::set_level(spdlog::level::debug);
    #endif
    std::string botName = argv[1];
    BotClient bot(botName);
    bot.run();
    return 0;
}
