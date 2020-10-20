# Point sysdir to iOS SDK
export SDKROOT=/Applications/Xcode.app/Contents/Developer/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk

# Create object files with iOS architecture
gcc -c enet.c -fembed-bitcode -target arm64-apple-ios

# Create static library
libtool -static enet.o -o libenet.a

rm enet.o