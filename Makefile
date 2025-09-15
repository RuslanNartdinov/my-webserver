NAME       := webserv
CXX        := c++
CXXFLAGS   := -Wall -Wextra -Werror -std=c++98
INCLUDES   := -Iinclude
BUILD_DIR  := build

# исходники во всех поддиректориях src/
SRC_DIRS   := src src/core src/config src/net src/http src/utils src/fs
SRCS       := $(foreach dir,$(SRC_DIRS),$(wildcard $(dir)/*.cpp))
OBJS       := $(patsubst src/%.cpp,$(BUILD_DIR)/%.o,$(SRCS))

.PHONY: all clean fclean re run

all: $(NAME)

$(NAME): $(OBJS)
	@$(CXX) $(CXXFLAGS) $(INCLUDES) $^ -o $@
	@echo "Linked -> $(NAME)"

# правило сборки объектников
$(BUILD_DIR)/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	@$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@
	@echo "Compiled $<"

run: $(NAME)
	@./$(NAME) examples/basic.conf

clean:
	@rm -rf $(BUILD_DIR)
	@echo "Cleaned objects"

fclean: clean
	@rm -f $(NAME)
	@echo "Removed binary"

re: fclean all