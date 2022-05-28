package test;

public class C {

	static {
		System.out.println("C 静态");
	}
	private static String ss = getStr1("ss");
	private static String getStr1(String str) {
		System.out.println("getser：" + str);
		return "getser";
	}

	{
		System.out.println("C 动态");
	}


	private String a = getStr("a");
	private String getStr(String str) {
		System.out.println("getser：" + str);
		return "getser";
	}



	public C() {
		System.out.println("C无参构造");
	}

	public C(String str) {
		System.out.println("C有参构造-str");
	}

	public C(String str, int m) {
		System.out.println("C有参构造-str,m");
	}
}
