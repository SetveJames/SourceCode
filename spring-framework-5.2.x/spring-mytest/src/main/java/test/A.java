package test;

import java.io.InputStream;
import java.lang.reflect.Constructor;

public class A extends B {


	public A(){
		this("hkhk",10);
	}

	public A(String str) {
		this("hkhk",10);
	}

	public A(String str, int m) {
		super(str,m);
	}

	public static void main(String[] args) throws Exception {
//		new A();
//		new A("ds");

//		Class<A> aClass = A.class;
//		Constructor<A> constructor = aClass.getConstructor(String.class);
//		A a = constructor.newInstance("sss");

		ClassLoader classLoader = A.class.getClassLoader();
		InputStream resourceAsStream = classLoader.getResourceAsStream("aaa.xml");
		System.out.println("ok");

	}
}
