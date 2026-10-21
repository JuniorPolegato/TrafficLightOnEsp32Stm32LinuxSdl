#!/bin/bash

# Isolated directory to hold our proxy links
PROXY_DIR="$HOME/.pio_ccache_links"
rm -rf "$PROXY_DIR"
mkdir -p "$PROXY_DIR"

TOOLCHAINS=(
    "$HOME/.platformio/packages/toolchain-xtensa-esp-elf"
    "$HOME/.platformio/packages/toolchain-riscv32-esp"
    "$HOME/.platformio/packages/toolchain-xtensa"
)

echo "Building ccache proxy directory..."

for TC_DIR in "${TOOLCHAINS[@]}"; do
    BIN_DIR="$TC_DIR/bin"
    if [ -d "$BIN_DIR" ]; then
        for FILE_PATH in "$BIN_DIR"/*; do
            if [ -f "$FILE_PATH" ] && [ ! -L "$FILE_PATH" ]; then
                FILENAME=$(basename "$FILE_PATH")

                # If it's a compiler binary, create a proxy wrapper script in our isolated folder
                if [[ "$FILENAME" == *"-g++" ]] || [[ "$FILENAME" == *"-gcc" ]] || [[ "$FILENAME" == *"-cpp" ]] || [[ "$FILENAME" == *"-as" ]]; then
                    cat << EOF > "$PROXY_DIR/$FILENAME"
#!/bin/bash
exec ccache "$FILE_PATH" "\$@"
EOF
                    chmod +x "$PROXY_DIR/$FILENAME"
                fi
            fi
        done
    fi
done

echo "Proxy setup complete at: $PROXY_DIR"
