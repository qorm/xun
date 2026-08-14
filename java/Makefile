.PHONY: test clean

SRC = src/io/github/qorm/xun/Xun.java
TEST = test/io/github/qorm/xun/XunTest.java
OUT = build

test: $(OUT)/io/github/qorm/xun/Xun.class $(OUT)/io/github/qorm/xun/XunTest.class
	java -cp $(OUT) io.github.qorm.xun.XunTest ..

$(OUT)/io/github/qorm/xun/Xun.class: $(SRC)
	mkdir -p $(OUT)
	javac -d $(OUT) $(SRC)

$(OUT)/io/github/qorm/xun/XunTest.class: $(TEST) $(OUT)/io/github/qorm/xun/Xun.class
	javac -cp $(OUT) -d $(OUT) $(TEST)

clean:
	rm -rf $(OUT)
