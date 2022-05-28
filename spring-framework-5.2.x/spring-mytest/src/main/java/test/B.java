package test;

public class B extends C{

//	static {
//		System.out.println("B 静态");
//	}
//
//	{
//		System.out.println("B 动态");
//	}

//	public B() {
//		//System.out.println("B无参构造");
//	}

	public B(String str) {
		System.out.println("B有参构造-str");
	}

	public B(String str, int m) {
		System.out.println("B有参构造-str,m");
	}
}
