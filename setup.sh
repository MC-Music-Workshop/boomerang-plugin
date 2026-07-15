#!/bin/bash

# Turrama JUCE Plugin Setup Script
# This script sets up the development environment for building the plugin

echo "Setting up Turrama JUCE Plugin Development Environment..."

# Check if JUCE is already available
if [ ! -d "JUCE" ]; then
    echo "Cloning JUCE framework..."
    git clone --depth 1 --branch 8.0.10 https://github.com/juce-framework/JUCE.git
else
    echo "JUCE already exists, skipping clone..."
fi

# Create build directory
echo "Creating build directory..."
mkdir -p build

echo "Setup complete!"
echo ""
echo "To build the plugin:"
echo "1. cd build"
echo "2. cmake .."
echo "3. make"
echo ""
echo "The plugin will be built in the following formats:"
echo "- VST3: ~/Library/Audio/Plug-Ins/VST3/"
echo "- AU: ~/Library/Audio/Plug-Ins/Components/"
echo "- AAX: ~/Library/Application Support/Avid/Audio/Plug-Ins/"