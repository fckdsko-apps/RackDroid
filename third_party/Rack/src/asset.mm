#import <string>
#import <Foundation/Foundation.h>


namespace rack {
namespace asset {


std::string getApplicationSupportDir() {
	@autoreleasepool {
		NSArray* paths = NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES);
		NSString* path = [paths firstObject];
		return [path UTF8String];
	}
}


} // namespace asset
} // namespace rack
